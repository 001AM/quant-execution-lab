#include "quant_engine/order.hpp"

#include "test_support.hpp"

#include <stdexcept>

namespace {
using namespace quant_engine;

Order make_order(const Quantity quantity = 100) {
    return Order{1, Side::Buy, OrderType::Limit, 10'025, quantity, 42};
}

void valid_order_creation() {
    const Order order = make_order();
    CHECK(order.order_id() == 1);
    CHECK(order.side() == Side::Buy);
    CHECK(order.type() == OrderType::Limit);
    CHECK(order.price() == 10'025);
    CHECK(order.original_quantity() == 100);
    CHECK(order.remaining_quantity() == 100);
    CHECK(order.sequence() == 42);
    CHECK(order.status() == OrderStatus::New);
    CHECK(!order.is_active());
}

void invalid_quantity_is_rejected() {
    EXPECT_THROW(Order(1, Side::Buy, OrderType::Limit, 100, 0, 1),
                 std::invalid_argument);
}

void invalid_limit_price_is_rejected() {
    EXPECT_THROW(Order(1, Side::Buy, OrderType::Limit, 0, 10, 1),
                 std::invalid_argument);
    EXPECT_THROW(Order(1, Side::Sell, OrderType::Limit, -1, 10, 1),
                 std::invalid_argument);
}

void partial_fill_updates_status_and_remaining() {
    Order order = make_order();
    order.activate();
    order.fill(40);
    CHECK(order.remaining_quantity() == 60);
    CHECK(order.filled_quantity() == 40);
    CHECK(order.status() == OrderStatus::PartiallyFilled);
}

void full_fill_updates_status() {
    Order order = make_order();
    order.activate();
    order.fill(100);
    CHECK(order.remaining_quantity() == 0);
    CHECK(order.status() == OrderStatus::Filled);
    CHECK(!order.is_active());
}

void overfill_is_rejected_without_mutation() {
    Order order = make_order();
    order.activate();
    EXPECT_THROW(order.fill(101), std::invalid_argument);
    CHECK(order.remaining_quantity() == 100);
    CHECK(order.status() == OrderStatus::Active);
}

void cancellation_changes_only_lifecycle_state() {
    Order order = make_order();
    order.activate();
    order.fill(40);
    order.cancel();
    CHECK(order.status() == OrderStatus::Cancelled);
    CHECK(order.remaining_quantity() == 60);
    CHECK(order.filled_quantity() == 40);
}

void terminal_orders_reject_invalid_transitions() {
    Order filled = make_order();
    filled.activate();
    filled.fill(100);
    EXPECT_THROW(filled.cancel(), std::logic_error);
    EXPECT_THROW(filled.fill(1), std::logic_error);

    Order cancelled{2, Side::Sell, OrderType::Limit, 100, 10, 43};
    cancelled.activate();
    cancelled.cancel();
    EXPECT_THROW(cancelled.fill(1), std::logic_error);
    EXPECT_THROW(cancelled.cancel(), std::logic_error);
}

void original_quantity_is_preserved() {
    Order order = make_order();
    order.activate();
    order.fill(25);
    order.fill(25);
    CHECK(order.original_quantity() == 100);
    CHECK(order.remaining_quantity() == 50);
}

}  // namespace

int main() {
    valid_order_creation();
    invalid_quantity_is_rejected();
    invalid_limit_price_is_rejected();
    partial_fill_updates_status_and_remaining();
    full_fill_updates_status();
    overfill_is_rejected_without_mutation();
    cancellation_changes_only_lifecycle_state();
    terminal_orders_reject_invalid_transitions();
    original_quantity_is_preserved();
    return test_support::failures == 0 ? 0 : 1;
}
