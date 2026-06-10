#include "obm/TickDataReplayer.hpp"
#include <sstream>
#include <string>

namespace obm {

TickDataReplayer::TickDataReplayer(const std::string& path, Symbol symbol)
    : file_(path), symbol_(symbol) {
    if (file_.is_open()) {
        std::string header;
        std::getline(file_, header); // skip CSV header
    }
}

std::optional<OrderEvent> TickDataReplayer::next() {
    std::string line;
    while (std::getline(file_, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ss(line);
        std::string type_s, side_s, price_s, qty_s, vis_s, id_s;
        if (!std::getline(ss, type_s,  ',')) continue;
        if (!std::getline(ss, side_s,  ',')) continue;
        if (!std::getline(ss, price_s, ',')) continue;
        if (!std::getline(ss, qty_s,   ',')) continue;
        if (!std::getline(ss, vis_s,   ',')) continue;
        if (!std::getline(ss, id_s,    ',')) continue;

        OrderEvent ev{};
        ev.symbol   = symbol_;
        ev.order_id = static_cast<OrderId>(std::stoull(id_s));
        ev.qty      = static_cast<Quantity>(std::stoull(qty_s));
        ev.visible_qty = static_cast<Quantity>(std::stoull(vis_s));

        char t = type_s.empty() ? 'L' : type_s[0];
        if (t == 'C') {
            ev.type     = OrderEvent::Type::CANCEL_ORDER;
            ev.order_id = ev.order_id;
            ++count_;
            return ev;
        }

        ev.type = OrderEvent::Type::NEW_ORDER;
        ev.side = (side_s[0] == 'B' || side_s[0] == 'b') ? Side::BUY : Side::SELL;
        ev.price = double_to_price(std::stod(price_s));
        ev.tif  = TimeInForce::GTC;

        switch (t) {
            case 'M': ev.order_type = OrderType::MARKET; break;
            case 'I': ev.order_type = OrderType::IOC;    break;
            case 'F': ev.order_type = OrderType::FOK;    break;
            case 'B': ev.order_type = OrderType::ICEBERG;break;
            default:  ev.order_type = OrderType::LIMIT;  break;
        }

        ++count_;
        return ev;
    }
    return std::nullopt;
}

} // namespace obm
