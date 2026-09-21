#include "quant_engine/price_level.hpp"

#include "test_support.hpp"

#include <stdexcept>

namespace {
using namespace quant_engine;

Order make_order(const OrderId id, const Quantity quantity, const SequenceNumber sequence,
                 const Price price = 100) {
    Order order{id, Side::Buy, OrderType::Limit, price, quantity, sequence};
    order.activate();
    return order;
}

void add_order_updates_level() {
    PriceLevel level{100};
    Order order = make_order(1, 100, 1);
    level.add_order(order);
    CHECK(level.price() == 100);
    CHECK(level.size() == 1);
    CHECK(level.total_quantity() == 100);
}

void fifo_ordering_is_preserved() {
    PriceLevel level{100};
    Order first = make_order(1, 10, 1);
    Order second = make_order(2, 20, 2);
    level.add_order(first);
    level.add_order(second);
    CHECK(level.front().order_id() == 1);
    CHECK(level.remove_order(1));
    CHECK(level.front().order_id() == 2);
}

void remove_front_updates_quantity() {
    PriceLevel level{100};
    Order first = make_order(1, 10, 1);
    Order second = make_order(2, 20, 2);
    level.add_order(first);
    level.add_order(second);
    CHECK(level.remove_order(1));
    CHECK(level.total_quantity() == 20);
    CHECK(level.size() == 1);
}

void remove_middle_is_constant_time_by_id() {
    PriceLevel level{100};
    Order first = make_order(1, 10, 1);
    Order middle = make_order(2, 20, 2);
    Order last = make_order(3, 30, 3);
    level.add_order(first);
    level.add_order(middle);
    level.add_order(last);
    CHECK(level.remove_order(2));
    CHECK(level.size() == 2);
    CHECK(level.total_quantity() == 40);
    CHECK(level.front().order_id() == 1);
}

void total_quantity_sums_remaining_orders() {
    PriceLevel level{100};
    Order first = make_order(1, 100, 1);
    Order second = make_order(2, 200, 2);
    Order third = make_order(3, 300, 3);
    level.add_order(first);
    level.add_order(second);
    level.add_order(third);
    CHECK(level.total_quantity() == 600);
}

void empty_level_is_safe() {
    PriceLevel level{100};
    CHECK(level.empty());
    CHECK(level.size() == 0);
    CHECK(level.total_quantity() == 0);
    CHECK(!level.remove_order(999));
    EXPECT_THROW(level.front(), std::out_of_range);
}

void partial_fill_updates_order_and_level_quantity() {
    PriceLevel level{100};
    Order order = make_order(1, 100, 1);
    level.add_order(order);
    level.fill_front(40);
    CHECK(level.total_quantity() == 60);
    CHECK(level.front().remaining_quantity() == 60);
    CHECK(level.front().status() == OrderStatus::PartiallyFilled);
}

void invalid_membership_is_rejected() {
    PriceLevel level{100};
    Order wrong_price = make_order(1, 10, 1, 101);
    EXPECT_THROW(level.add_order(wrong_price), std::invalid_argument);

    Order valid = make_order(2, 10, 2);
    level.add_order(valid);
    EXPECT_THROW(level.add_order(valid), std::invalid_argument);
}

}  // namespace

int main() {
    add_order_updates_level();
    fifo_ordering_is_preserved();
    remove_front_updates_quantity();
    remove_middle_is_constant_time_by_id();
    total_quantity_sums_remaining_orders();
    empty_level_is_safe();
    partial_fill_updates_order_and_level_quantity();
    invalid_membership_is_rejected();
    return test_support::failures == 0 ? 0 : 1;
}
