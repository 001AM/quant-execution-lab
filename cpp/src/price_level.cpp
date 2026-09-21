#include "quant_engine/price_level.hpp"

#include <limits>
#include <stdexcept>

namespace quant_engine {

PriceLevel::PriceLevel(const Price price) : price_{price} {
    if (price <= 0) {
        throw std::invalid_argument("price level must have a positive price");
    }
}

void PriceLevel::reserve(const std::size_t expected_orders) {
    order_index_.reserve(expected_orders);
}

void PriceLevel::add_order(Order& order) {
    if (order.type() != OrderType::Limit) {
        throw std::invalid_argument("price levels accept only limit orders");
    }
    if (order.price() != price_) {
        throw std::invalid_argument("order price does not match price level");
    }
    if (!order.is_active() || order.remaining_quantity() == 0) {
        throw std::invalid_argument("only active orders can rest at a price level");
    }
    if (order_index_.contains(order.order_id())) {
        throw std::invalid_argument("duplicate order id at price level");
    }
    if (order.remaining_quantity() >
        std::numeric_limits<Quantity>::max() - total_quantity_) {
        throw std::overflow_error("price-level total quantity overflow");
    }

    orders_.push_back(&order);
    const auto iterator = std::prev(orders_.end());
    order_index_.emplace(order.order_id(), iterator);
    total_quantity_ += order.remaining_quantity();
}

bool PriceLevel::remove_order(const OrderId order_id) {
    const auto found = order_index_.find(order_id);
    if (found == order_index_.end()) {
        return false;
    }
    const Order* order = *found->second;
    total_quantity_ -= order->remaining_quantity();
    orders_.erase(found->second);
    order_index_.erase(found);
    return true;
}

Order& PriceLevel::front() {
    if (orders_.empty()) {
        throw std::out_of_range("price level is empty");
    }
    return **orders_.begin();
}

const Order& PriceLevel::front() const {
    if (orders_.empty()) {
        throw std::out_of_range("price level is empty");
    }
    return **orders_.begin();
}

void PriceLevel::fill_front(const Quantity quantity) {
    Order& order = front();
    order.fill(quantity);
    total_quantity_ -= quantity;
}

void PriceLevel::fill_front(const Quantity quantity, const Price execution_price) {
    Order& order = front();
    order.fill(quantity, execution_price);
    total_quantity_ -= quantity;
}

void PriceLevel::modify_quantity(Order& order, const Quantity new_total_quantity) {
    const auto found = order_index_.find(order.order_id());
    if (found == order_index_.end() || *found->second != &order) {
        throw std::invalid_argument("order does not belong to this price level");
    }
    const Quantity old_remaining = order.remaining_quantity();
    order.replace(order.price(), new_total_quantity, order.sequence());
    if (!order.is_active()) {
        throw std::logic_error("zero-remainder replacement must be removed first");
    }
    const Quantity new_remaining = order.remaining_quantity();
    if (new_remaining >= old_remaining) {
        total_quantity_ += new_remaining - old_remaining;
    } else {
        total_quantity_ -= old_remaining - new_remaining;
    }
}

bool PriceLevel::contains(const OrderId order_id) const noexcept {
    return order_index_.contains(order_id);
}

std::vector<OrderId> PriceLevel::order_ids() const {
    std::vector<OrderId> result;
    result.reserve(orders_.size());
    for (const Order* order : orders_) {
        result.push_back(order->order_id());
    }
    return result;
}

bool PriceLevel::validate_invariants() const noexcept {
    if (orders_.empty() || orders_.size() != order_index_.size()) {
        return false;
    }
    Quantity calculated_total = 0;
    for (const Order* order : orders_) {
        if (order == nullptr || !order->is_active() || order->type() != OrderType::Limit ||
            order->price() != price_ || order->remaining_quantity() == 0 ||
            !order_index_.contains(order->order_id())) {
            return false;
        }
        if (order->remaining_quantity() >
            std::numeric_limits<Quantity>::max() - calculated_total) {
            return false;
        }
        calculated_total += order->remaining_quantity();
    }
    return calculated_total == total_quantity_;
}

}  // namespace quant_engine
