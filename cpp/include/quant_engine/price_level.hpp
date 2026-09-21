#pragma once

#include "quant_engine/order.hpp"

#include <cstddef>
#include <list>
#include <unordered_map>
#include <vector>

namespace quant_engine {

class PriceLevel {
public:
    explicit PriceLevel(Price price);

    [[nodiscard]] Price price() const noexcept { return price_; }
    [[nodiscard]] bool empty() const noexcept { return orders_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return orders_.size(); }
    [[nodiscard]] Quantity total_quantity() const noexcept {
        return total_quantity_;
    }

    void reserve(std::size_t expected_orders);

    void add_order(Order& order);
    [[nodiscard]] bool remove_order(OrderId order_id);
    [[nodiscard]] Order& front();
    [[nodiscard]] const Order& front() const;
    void fill_front(Quantity quantity);
    void fill_front(Quantity quantity, Price execution_price);
    void modify_quantity(Order& order, Quantity new_total_quantity);
    [[nodiscard]] bool contains(OrderId order_id) const noexcept;
    [[nodiscard]] std::vector<OrderId> order_ids() const;
    [[nodiscard]] bool validate_invariants() const noexcept;

private:
    using Queue = std::list<Order*>;
    using Iterator = Queue::iterator;

    Price price_;
    Quantity total_quantity_{0};
    Queue orders_;
    std::unordered_map<OrderId, Iterator> order_index_;
};

}  // namespace quant_engine
