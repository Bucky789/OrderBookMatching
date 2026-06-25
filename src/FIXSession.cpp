#include "obm/FIXSession.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <mutex>

namespace obm {

// ── File-local FIX wire helpers ───────────────────────────────────────────────

static char* ftag(char* p, char* e, int tag, const char* val) noexcept {
    int n = std::snprintf(p, static_cast<std::size_t>(e - p), "%d=%s\x01", tag, val);
    return (n > 0 && p + n < e) ? p + n : e;
}
static char* ftag_i(char* p, char* e, int tag, int64_t val) noexcept {
    int n = std::snprintf(p, static_cast<std::size_t>(e - p),
                          "%d=%lld\x01", tag, static_cast<long long>(val));
    return (n > 0 && p + n < e) ? p + n : e;
}
static char* ftag_price(char* p, char* e, int tag, Price price) noexcept {
    int64_t ip = price / PRICE_SCALE;
    int64_t fp = price % PRICE_SCALE;
    int n = std::snprintf(p, static_cast<std::size_t>(e - p),
                          "%d=%lld.%08lld\x01", tag,
                          static_cast<long long>(ip),
                          static_cast<long long>(fp));
    return (n > 0 && p + n < e) ? p + n : e;
}

// ── FIXSession ────────────────────────────────────────────────────────────────

FIXSession::FIXSession(MatchingEngine& engine, Config cfg)
    : engine_(engine), cfg_(std::move(cfg)) {}

FIXSession::~FIXSession() { cleanup(); }

void FIXSession::stop() noexcept { stop_.store(true, std::memory_order_release); }

// ── Public run() ──────────────────────────────────────────────────────────────

void FIXSession::run() {
    // Register fill callback (must be called before engine.run())
    engine_.on_fill([this](const Fill& f) { send_exec_report(f); });

    if (!init_listen()) return;

    while (!stop_.load(std::memory_order_relaxed)) {
        state_.store(State::WAITING_LOGON, std::memory_order_relaxed);
        out_seq_ = 1;
        in_seq_  = 1;

        if (!accept_client()) break;

        // Session loop
        last_send_ns_ = last_recv_ns_ = monotonic_ns();

        while (!stop_.load(std::memory_order_relaxed)) {
            int n = recv_data();
            if (n < 0) break;           // connection closed or error
            if (n > 0) {
                recv_len_ += static_cast<std::size_t>(n);
                process_buffer();
                last_recv_ns_ = monotonic_ns();
            }

            const int64_t now         = monotonic_ns();
            const int64_t hb_ns       = static_cast<int64_t>(cfg_.heartbeat_secs) * 1'000'000'000LL;
            const State   cur_state   = state_.load(std::memory_order_relaxed);

            // Send heartbeat if overdue
            if (cur_state == State::ACTIVE && now - last_send_ns_ >= hb_ns) {
                send_heartbeat();
            }

            // Drop connection if peer silent for 2× heartbeat interval
            if (cur_state == State::ACTIVE && now - last_recv_ns_ > 2 * hb_ns) {
                std::fprintf(stderr, "[FIX] heartbeat timeout — disconnecting\n");
                break;
            }

            if (cur_state == State::LOGGING_OUT) break;
        }

        close_client();
    }
    cleanup();
}

// ── Socket lifecycle ──────────────────────────────────────────────────────────

bool FIXSession::init_listen() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fprintf(stderr, "[FIX] WSAStartup failed\n");
        return false;
    }
#endif
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        std::fprintf(stderr, "[FIX] socket() failed\n");
        return false;
    }
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(cfg_.port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (cfg_.host != "0.0.0.0") {
        inet_pton(AF_INET, cfg_.host.c_str(), &addr.sin_addr);
    }

    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::fprintf(stderr, "[FIX] bind() failed on port %u\n", cfg_.port);
        closesocket(s);
        return false;
    }
    if (listen(s, 1) != 0) {
        closesocket(s);
        return false;
    }
    listen_sock_ = static_cast<uintptr_t>(s);
    std::fprintf(stderr, "[FIX] listening on port %u\n", cfg_.port);
    return true;
}

bool FIXSession::accept_client() {
    sockaddr_in peer{};
    int peer_len = sizeof(peer);
    SOCKET c = accept(static_cast<SOCKET>(listen_sock_),
                      reinterpret_cast<sockaddr*>(&peer),
                      &peer_len);
    if (c == INVALID_SOCKET) return false;

    // 100 ms receive timeout so session loop can check heartbeats
    DWORD tv_ms = 100;
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&tv_ms), sizeof(tv_ms));

    client_sock_ = static_cast<uintptr_t>(c);
    char ip[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
    std::fprintf(stderr, "[FIX] client connected from %s:%u\n",
                 ip, ntohs(peer.sin_port));
    recv_len_ = 0;
    return true;
}

void FIXSession::close_client() noexcept {
    if (client_sock_ != ~uintptr_t(0)) {
        closesocket(static_cast<SOCKET>(client_sock_));
        client_sock_ = ~uintptr_t(0);
        std::fprintf(stderr, "[FIX] client disconnected\n");
    }
}

void FIXSession::cleanup() noexcept {
    close_client();
    if (listen_sock_ != ~uintptr_t(0)) {
        closesocket(static_cast<SOCKET>(listen_sock_));
        listen_sock_ = ~uintptr_t(0);
    }
#ifdef _WIN32
    WSACleanup();
#endif
}

// ── I/O ───────────────────────────────────────────────────────────────────────

bool FIXSession::send_all(const char* buf, std::size_t len) noexcept {
    if (client_sock_ == ~uintptr_t(0)) return false;
    SOCKET s = static_cast<SOCKET>(client_sock_);
    std::size_t sent = 0;
    while (sent < len) {
        int n = send(s, buf + sent, static_cast<int>(len - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    last_send_ns_ = monotonic_ns();
    return true;
}

int FIXSession::recv_data() noexcept {
    if (client_sock_ == ~uintptr_t(0)) return -1;
    if (recv_len_ >= RECV_CAP) return -1;  // buffer full — client sending garbage
    SOCKET s   = static_cast<SOCKET>(client_sock_);
    int n = recv(s, recv_buf_ + recv_len_,
                 static_cast<int>(RECV_CAP - recv_len_), 0);
    if (n == 0) return -1;  // connection closed
#ifdef _WIN32
    if (n == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK) return 0;
        return -1;
    }
#else
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
#endif
    return n;
}

// ── Message parsing ───────────────────────────────────────────────────────────

void FIXSession::process_buffer() {
    // Scan for complete FIX messages: each ends with \x0110=NNN\x01
    static const char CHECKSUM_TAG[] = "\x01" "10=";
    while (recv_len_ > 0) {
        const char* p = static_cast<const char*>(
            std::search(recv_buf_, recv_buf_ + recv_len_,
                        CHECKSUM_TAG, CHECKSUM_TAG + 4));
        if (p == recv_buf_ + recv_len_) break;  // no complete message yet
        const char* eom = static_cast<const char*>(
            std::memchr(p + 4, '\x01', recv_buf_ + recv_len_ - (p + 4)));
        if (!eom) break;  // checksum field not complete yet

        std::size_t msg_len = static_cast<std::size_t>(eom - recv_buf_) + 1;
        handle_message(recv_buf_, msg_len);

        // Shift remaining bytes to front of buffer
        std::size_t remaining = recv_len_ - msg_len;
        if (remaining > 0) std::memmove(recv_buf_, recv_buf_ + msg_len, remaining);
        recv_len_ = remaining;
    }
}

void FIXSession::handle_message(const char* buf, std::size_t len) {
    FIXMessage msg{};
    if (!FIXParser::parse(buf, len, msg)) {
        std::fprintf(stderr, "[FIX] parse error: %s\n",
                     msg.error_msg ? msg.error_msg : "unknown");
        return;
    }

    // Sequence number check (warn only; no resend request in this demo)
    if (msg.seq_num != 0 && msg.seq_num != in_seq_) {
        std::fprintf(stderr, "[FIX] seq gap: expected %d got %d\n",
                     in_seq_, msg.seq_num);
    }
    if (msg.seq_num > 0) in_seq_ = msg.seq_num + 1;

    switch (msg.msg_type) {
    case 'A': handle_logon(msg);        break;
    case '5': handle_logout(msg);       break;
    case '0': handle_heartbeat(msg);    break;
    case '1': handle_test_request(msg); break;
    case 'D': handle_new_order(msg);    break;
    case 'F': handle_cancel(msg);       break;
    default:
        std::fprintf(stderr, "[FIX] unknown msg type '%c'\n", msg.msg_type);
        break;
    }
}

void FIXSession::handle_logon(const FIXMessage&) {
    if (state_.load() != State::WAITING_LOGON) return;
    state_.store(State::ACTIVE, std::memory_order_release);
    send_logon();
    std::fprintf(stderr, "[FIX] session active\n");
}

void FIXSession::handle_logout(const FIXMessage&) {
    state_.store(State::LOGGING_OUT, std::memory_order_release);
    send_logout("goodbye");
    std::fprintf(stderr, "[FIX] logout received\n");
}

void FIXSession::handle_heartbeat(const FIXMessage&) {
    // last_recv_ns_ updated in run() loop after recv_data()
}

void FIXSession::handle_test_request(const FIXMessage& m) {
    send_heartbeat(m.test_req_id);
}

void FIXSession::handle_new_order(const FIXMessage& m) {
    if (state_.load() != State::ACTIVE) return;
    OrderEvent ev = FIXParser::to_order_event(m, cfg_.default_symbol);
    while (!engine_.submit(ev)) {}  // spin if ring full
}

void FIXSession::handle_cancel(const FIXMessage& m) {
    if (state_.load() != State::ACTIVE) return;
    OrderEvent ev = FIXParser::to_order_event(m, cfg_.default_symbol);
    while (!engine_.submit(ev)) {}
}

// ── Outbound messages ─────────────────────────────────────────────────────────

std::size_t FIXSession::encode_msg(char* out, std::size_t sz,
                                   char msg_type,
                                   const char* body, std::size_t body_len) {
    // Build: body_fields = "35=T\x01 49=... 56=... 34=N\x01 52=TS\x01" + extra
    char header_fields[256];
    char ts[32]; utc_timestamp(ts, sizeof(ts));
    char mtype[2] = {msg_type, '\0'};
    char* p = header_fields;
    char* e = header_fields + sizeof(header_fields);
    p = ftag(p, e, 35, mtype);
    p = ftag(p, e, 49, cfg_.sender_comp_id.c_str());
    p = ftag(p, e, 56, cfg_.target_comp_id.c_str());
    p = ftag_i(p, e, 34, out_seq_++);
    p = ftag(p, e, 52, ts);
    std::size_t hflen = static_cast<std::size_t>(p - header_fields);

    // Total body = header_fields + body
    std::size_t total_body = hflen + body_len;
    if (total_body + 64 > sz) return 0;

    // Write: "8=FIX.4.2\x019=<total_body>\x01" + header_fields + body
    char pre[48];
    int plen = std::snprintf(pre, sizeof(pre),
                             "8=FIX.4.2\x019=%zu\x01",
                             total_body);
    if (plen <= 0) return 0;

    std::size_t pos = 0;
    std::memcpy(out + pos, pre, static_cast<std::size_t>(plen)); pos += plen;
    std::memcpy(out + pos, header_fields, hflen);                pos += hflen;
    std::memcpy(out + pos, body, body_len);                       pos += body_len;

    uint8_t chk = 0;
    for (std::size_t i = 0; i < pos; ++i) chk += static_cast<uint8_t>(out[i]);
    int clen = std::snprintf(out + pos, sz - pos, "10=%03u\x01",
                             static_cast<unsigned>(chk));
    if (clen <= 0) return 0;
    pos += static_cast<std::size_t>(clen);
    return pos;
}

void FIXSession::send_logon() {
    char body[64];
    char* p = body; char* e = body + sizeof(body);
    p = ftag_i(p, e, 98,  0);                   // EncryptMethod=0
    p = ftag_i(p, e, 108, cfg_.heartbeat_secs);  // HeartBtInt

    std::lock_guard<std::mutex> lk(send_mtx_);
    std::size_t n = encode_msg(send_buf_, SEND_CAP, 'A', body, p - body);
    if (n) send_all(send_buf_, n);
}

void FIXSession::send_logout(const char* text) {
    char body[128]{};
    char* p = body; char* e = body + sizeof(body);
    if (text && *text) p = ftag(p, e, 58, text);

    std::lock_guard<std::mutex> lk(send_mtx_);
    std::size_t n = encode_msg(send_buf_, SEND_CAP, '5', body, p - body);
    if (n) send_all(send_buf_, n);
}

void FIXSession::send_heartbeat(const char* test_req_id) {
    char body[64]{};
    char* p = body; char* e = body + sizeof(body);
    if (test_req_id && *test_req_id) p = ftag(p, e, 112, test_req_id);

    std::lock_guard<std::mutex> lk(send_mtx_);
    std::size_t n = encode_msg(send_buf_, SEND_CAP, '0', body, p - body);
    if (n) send_all(send_buf_, n);
}

void FIXSession::send_exec_report(const Fill& f) {
    if (state_.load(std::memory_order_relaxed) != State::ACTIVE) return;

    char body[256];
    char* p = body; char* e = body + sizeof(body);
    p = ftag_i(p, e, 37,  static_cast<int64_t>(f.aggressive_order_id));
    p = ftag_i(p, e, 17,  static_cast<int64_t>(f.timestamp_ns));  // ExecID
    p = ftag(p, e, 150, "2");   // ExecType=Trade
    p = ftag(p, e, 39,  "2");   // OrdStatus=Filled
    p = ftag_price(p, e, 31, f.fill_price);
    p = ftag_i(p, e, 32,  static_cast<int64_t>(f.fill_qty));

    std::lock_guard<std::mutex> lk(send_mtx_);
    std::size_t n = encode_msg(send_buf_, SEND_CAP, '8', body, p - body);
    if (n) send_all(send_buf_, n);
}

// ── Utilities ─────────────────────────────────────────────────────────────────

int64_t FIXSession::monotonic_ns() noexcept {
    using namespace std::chrono;
    return static_cast<int64_t>(
        steady_clock::now().time_since_epoch().count());
}

void FIXSession::utc_timestamp(char* buf, std::size_t sz) noexcept {
#ifdef _WIN32
    SYSTEMTIME st;
    GetSystemTime(&st);
    std::snprintf(buf, sz, "%04d%02d%02d-%02d:%02d:%02d.%03d",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm t;
    gmtime_r(&ts.tv_sec, &t);
    std::snprintf(buf, sz, "%04d%02d%02d-%02d:%02d:%02d.%03ld",
        t.tm_year+1900, t.tm_mon+1, t.tm_mday,
        t.tm_hour, t.tm_min, t.tm_sec,
        ts.tv_nsec / 1'000'000);
#endif
}

} // namespace obm
