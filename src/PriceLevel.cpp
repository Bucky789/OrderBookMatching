#include "obm/PriceLevel.hpp"
#include <cassert>
#include <utility>

namespace obm {

PriceLevel::PriceLevel(PriceLevel&& o) noexcept
    : head_(o.head_), tail_(o.tail_), total_qty_(o.total_qty_), count_(o.count_) {
    o.head_ = o.tail_ = nullptr;
    o.total_qty_ = 0;
    o.count_ = 0;
}

PriceLevel& PriceLevel::operator=(PriceLevel&& o) noexcept {
    if (this != &o) {
        head_ = o.head_; tail_ = o.tail_;
        total_qty_ = o.total_qty_; count_ = o.count_;
        o.head_ = o.tail_ = nullptr;
        o.total_qty_ = 0; o.count_ = 0;
    }
    return *this;
}

void PriceLevel::push_back(Order* o) noexcept {
    assert(o != nullptr);
    o->prev = tail_;
    o->next = nullptr;
    if (tail_) tail_->next = o;
    else       head_ = o;
    tail_ = o;
    total_qty_ += (o->type == OrderType::ICEBERG) ? o->visible_qty : o->remaining_qty;
    ++count_;
}

void PriceLevel::pop_front() noexcept {
    assert(head_ != nullptr);
    Order* old = head_;
    head_ = old->next;
    if (head_) head_->prev = nullptr;
    else       tail_ = nullptr;
    old->prev = old->next = nullptr;
    --count_;
    // Caller has already called reduce_qty() for fills against this order
}

void PriceLevel::remove(Order* o) noexcept {
    assert(o != nullptr);
    if (o->prev) o->prev->next = o->next;
    else         head_ = o->next;
    if (o->next) o->next->prev = o->prev;
    else         tail_ = o->prev;
    o->prev = o->next = nullptr;
    // Subtract remaining executable qty from total
    Quantity exec = (o->type == OrderType::ICEBERG)
                    ? (o->visible_qty + o->reserve_qty)
                    : o->remaining_qty;
    if (total_qty_ >= exec) total_qty_ -= exec;
    else                    total_qty_ = 0;
    --count_;
}

} // namespace obm
