#pragma once

#include "FIXEncoder.hpp"
#include "FIXParser.hpp"
#include "MatchingEngine.hpp"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace obm {

// Minimal FIX 4.2 TCP session.
//
// Protocol flow: accept → LOGON handshake → process orders/heartbeats → LOGOUT.
// Inbound:  Logon(A), Logout(5), Heartbeat(0), TestRequest(1),
//           NewOrderSingle(D), OrderCancelRequest(F).
// Outbound: Logon(A), Logout(5), Heartbeat(0), ExecutionReport(8).
//
// One connection at a time. Fills are sent as ExecutionReports from the
// engine's worker thread (protected by send_mtx_).
//
// Threading contract: register callbacks and call run() before engine.run().
class FIXSession {
public:
    struct Config {
        std::string host           = "0.0.0.0";
        uint16_t    port           = 9878;
        std::string sender_comp_id = "ENGINE";
        std::string target_comp_id = "CLIENT";
        int         heartbeat_secs = 30;     // tag 108
        Symbol      default_symbol = 1;
    };

    explicit FIXSession(MatchingEngine& engine, Config cfg = {});
    ~FIXSession();
    FIXSession(const FIXSession&)            = delete;
    FIXSession& operator=(const FIXSession&) = delete;

    // Listen, accept one client, run session until LOGOUT or error. Blocking.
    void run();

    // Thread-safe signal to stop after current session ends.
    void stop() noexcept;

private:
    enum class State : uint8_t { WAITING_LOGON, ACTIVE, LOGGING_OUT };

    MatchingEngine&       engine_;
    Config                cfg_;
    std::atomic<bool>     stop_{false};
    std::atomic<State>    state_{State::WAITING_LOGON};
    std::mutex            send_mtx_;   // protects socket writes + out_seq_

    int      out_seq_      = 1;
    int      in_seq_       = 1;
    int64_t  last_send_ns_ = 0;
    int64_t  last_recv_ns_ = 0;

    // Socket handles — uintptr_t matches SOCKET on both 32/64-bit Windows.
    uintptr_t listen_sock_ = ~uintptr_t(0);
    uintptr_t client_sock_ = ~uintptr_t(0);

    static constexpr std::size_t RECV_CAP = 65536;
    static constexpr std::size_t SEND_CAP = 4096;

    char        recv_buf_[RECV_CAP]{};
    std::size_t recv_len_ = 0;
    char        send_buf_[SEND_CAP]{};

    // Socket lifecycle
    bool init_listen();
    bool accept_client();
    void close_client() noexcept;
    void cleanup() noexcept;

    // I/O
    bool send_all(const char* buf, std::size_t len) noexcept;
    int  recv_data() noexcept; // >0 bytes, 0 = timeout, -1 = error/closed

    // Message dispatch
    void process_buffer();
    void handle_message(const char* buf, std::size_t len);
    void handle_logon(const FIXMessage& m);
    void handle_logout(const FIXMessage& m);
    void handle_heartbeat(const FIXMessage& m);
    void handle_test_request(const FIXMessage& m);
    void handle_new_order(const FIXMessage& m);
    void handle_cancel(const FIXMessage& m);

    // Outbound message builders (all acquire send_mtx_ internally)
    void send_logon();
    void send_logout(const char* text = "");
    void send_heartbeat(const char* test_req_id = "");
    void send_exec_report(const Fill& f);      // called from engine thread

    // Low-level FIX wire helpers
    std::size_t encode_msg(char* out, std::size_t sz, char msg_type,
                           const char* body, std::size_t body_len);

    static int64_t monotonic_ns() noexcept;
    static void    utc_timestamp(char* buf, std::size_t sz) noexcept;
};

} // namespace obm
