#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>

#include "obm/MatchingEngine.hpp"
#include "obm/RiskEngine.hpp"
#include "obm/Types.hpp"

namespace py = pybind11;
using namespace obm;

PYBIND11_MODULE(obm, m) {
    m.doc() = "HFT order book matching engine — Python bindings";

    // ── Price helpers ─────────────────────────────────────────────────────────
    m.def("double_to_price", &double_to_price,
          "Convert float price to fixed-point int64 (8 decimal places)");
    m.def("price_to_double", &price_to_double,
          "Convert fixed-point int64 to float price");
    m.attr("PRICE_SCALE")   = PRICE_SCALE;
    m.attr("INVALID_PRICE") = INVALID_PRICE;

    // ── Enumerations ──────────────────────────────────────────────────────────
    py::enum_<Side>(m, "Side")
        .value("BUY",  Side::BUY)
        .value("SELL", Side::SELL);

    py::enum_<OrderType>(m, "OrderType")
        .value("LIMIT",   OrderType::LIMIT)
        .value("MARKET",  OrderType::MARKET)
        .value("IOC",     OrderType::IOC)
        .value("FOK",     OrderType::FOK)
        .value("ICEBERG", OrderType::ICEBERG)
        .value("STOP",    OrderType::STOP);

    py::enum_<OrderState>(m, "OrderState")
        .value("NEW",              OrderState::NEW)
        .value("PARTIALLY_FILLED", OrderState::PARTIALLY_FILLED)
        .value("FILLED",           OrderState::FILLED)
        .value("CANCELLED",        OrderState::CANCELLED)
        .value("REJECTED",         OrderState::REJECTED);

    py::enum_<TimeInForce>(m, "TimeInForce")
        .value("DAY", TimeInForce::DAY)
        .value("GTC", TimeInForce::GTC)
        .value("IOC", TimeInForce::IOC)
        .value("FOK", TimeInForce::FOK);

    py::enum_<OrderEvent::Type>(m, "EventType")
        .value("NEW_ORDER",    OrderEvent::Type::NEW_ORDER)
        .value("CANCEL_ORDER", OrderEvent::Type::CANCEL_ORDER)
        .value("MODIFY_ORDER", OrderEvent::Type::MODIFY_ORDER)
        .value("SHUTDOWN",     OrderEvent::Type::SHUTDOWN);

    // ── Fill ──────────────────────────────────────────────────────────────────
    py::class_<Fill>(m, "Fill")
        .def_readonly("aggressive_order_id", &Fill::aggressive_order_id)
        .def_readonly("passive_order_id",    &Fill::passive_order_id)
        .def_readonly("symbol",              &Fill::symbol)
        .def_readonly("fill_price",          &Fill::fill_price)
        .def_readonly("fill_qty",            &Fill::fill_qty)
        .def_readonly("timestamp_ns",        &Fill::timestamp_ns)
        .def("fill_price_double", [](const Fill& f) {
            return price_to_double(f.fill_price);
        });

    // ── OrderEvent ────────────────────────────────────────────────────────────
    py::class_<OrderEvent>(m, "OrderEvent")
        .def(py::init<>())
        .def_readwrite("type",         &OrderEvent::type)
        .def_readwrite("order_type",   &OrderEvent::order_type)
        .def_readwrite("side",         &OrderEvent::side)
        .def_readwrite("tif",          &OrderEvent::tif)
        .def_readwrite("order_id",     &OrderEvent::order_id)
        .def_readwrite("client_order_id", &OrderEvent::client_order_id)
        .def_readwrite("symbol",       &OrderEvent::symbol)
        .def_readwrite("price",        &OrderEvent::price)
        .def_readwrite("stop_price",   &OrderEvent::stop_price)
        .def_readwrite("qty",          &OrderEvent::qty)
        .def_readwrite("visible_qty",  &OrderEvent::visible_qty)
        .def_readwrite("timestamp_ns", &OrderEvent::timestamp_ns);

    // ── MatchingEngine::Stats ─────────────────────────────────────────────────
    py::class_<MatchingEngine::Stats>(m, "Stats")
        .def_readonly("orders_received",  &MatchingEngine::Stats::orders_received)
        .def_readonly("orders_matched",   &MatchingEngine::Stats::orders_matched)
        .def_readonly("orders_cancelled", &MatchingEngine::Stats::orders_cancelled)
        .def_readonly("fills_generated",  &MatchingEngine::Stats::fills_generated)
        .def_readonly("volume_traded",    &MatchingEngine::Stats::volume_traded);

    // ── MatchingEngine::Config ────────────────────────────────────────────────
    py::class_<MatchingEngine::Config>(m, "EngineConfig")
        .def(py::init<>())
        .def_readwrite("pool_initial_slabs", &MatchingEngine::Config::pool_initial_slabs)
        .def_readwrite("n_partitions",       &MatchingEngine::Config::n_partitions);

    // ── MatchingEngine ────────────────────────────────────────────────────────
    py::class_<MatchingEngine>(m, "MatchingEngine")
        .def(py::init<MatchingEngine::Config>(), py::arg("cfg") = MatchingEngine::Config{})
        .def("submit",   &MatchingEngine::submit,
             "Submit OrderEvent to ring buffer. Returns False if ring full.")
        .def("run_once", &MatchingEngine::run_once,
             "Drain all partition rings once (single-threaded, for backtesting).")
        .def("shutdown", &MatchingEngine::shutdown)
        .def("stats",    &MatchingEngine::stats)
        .def("on_fill", [](MatchingEngine& eng, py::object cb) {
            // Capture callback; GIL must be held when cb is called.
            eng.on_fill([cb](const Fill& f) {
                py::gil_scoped_acquire gil;
                cb(f);
            });
        }, "Register Python fill callback: callback(Fill) -> None")
        .def("on_reject", [](MatchingEngine& eng, py::object cb) {
            eng.on_reject([cb](OrderId id, std::string_view reason) {
                py::gil_scoped_acquire gil;
                cb(id, std::string(reason));
            });
        }, "Register Python reject callback: callback(order_id, reason) -> None");

    // ── RiskEngine::Limits ────────────────────────────────────────────────────
    py::class_<RiskEngine::Limits>(m, "RiskLimits")
        .def(py::init<>())
        .def_readwrite("max_order_qty",      &RiskEngine::Limits::max_order_qty)
        .def_readwrite("max_order_notional", &RiskEngine::Limits::max_order_notional)
        .def_readwrite("max_net_position",   &RiskEngine::Limits::max_net_position)
        .def_readwrite("max_cancel_ratio",   &RiskEngine::Limits::max_cancel_ratio);

    // ── RiskEngine ────────────────────────────────────────────────────────────
    py::class_<RiskEngine>(m, "RiskEngine")
        .def(py::init<MatchingEngine&, RiskEngine::Limits>(),
             py::arg("engine"), py::arg("limits") = RiskEngine::Limits{})
        .def("submit", &RiskEngine::submit,
             "Submit with pre-trade risk checks. Returns False if rejected or ring full.")
        .def("on_fill", &RiskEngine::on_fill);
}
