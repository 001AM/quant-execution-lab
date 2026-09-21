#include "quant_engine/execution/parent_order.hpp"

#include "../test_support.hpp"

#include <chrono>
#include <stdexcept>

namespace {
using namespace quant_engine;
using namespace quant_engine::execution;

Timestamp minute(const int value) {
    return Timestamp{} + std::chrono::minutes{value};
}

ParentOrder make_parent(const Quantity quantity = 100) {
    return ParentOrder{1, "AAPL", Side::Buy, quantity, minute(0), minute(10)};
}

void valid_parent_starts_pending() {
    const ParentOrder parent = make_parent();
    CHECK(parent.parent_order_id() == 1);
    CHECK(parent.symbol() == "AAPL");
    CHECK(parent.total_quantity() == 100);
    CHECK(parent.executed_quantity() == 0);
    CHECK(parent.remaining_quantity() == 100);
    CHECK(parent.status() == ParentOrderStatus::Pending);
}

void zero_quantity_is_rejected() {
    EXPECT_THROW(ParentOrder(1, "AAPL", Side::Buy, 0, minute(0), minute(10)),
                 std::invalid_argument);
}

void invalid_time_range_is_rejected() {
    EXPECT_THROW(ParentOrder(1, "AAPL", Side::Buy, 10, minute(10), minute(10)),
                 std::invalid_argument);
    EXPECT_THROW(ParentOrder(1, "AAPL", Side::Buy, 10, minute(11), minute(10)),
                 std::invalid_argument);
}

void partial_execution_preserves_quantity_invariant() {
    ParentOrder parent = make_parent();
    parent.activate();
    parent.record_fill(40);
    CHECK(parent.status() == ParentOrderStatus::PartiallyFilled);
    CHECK(parent.executed_quantity() == 40);
    CHECK(parent.remaining_quantity() == 60);
    CHECK(parent.executed_quantity() + parent.remaining_quantity() ==
          parent.total_quantity());
}

void full_execution_is_terminal() {
    ParentOrder parent = make_parent();
    parent.activate();
    parent.record_fill(100);
    CHECK(parent.status() == ParentOrderStatus::Filled);
    CHECK(parent.is_terminal());
    CHECK(!parent.can_generate_children());
}

void cancellation_preserves_unexecuted_quantity() {
    ParentOrder parent = make_parent();
    parent.activate();
    parent.record_fill(25);
    parent.cancel();
    CHECK(parent.status() == ParentOrderStatus::Cancelled);
    CHECK(parent.executed_quantity() == 25);
    CHECK(parent.remaining_quantity() == 75);
}

void expiry_preserves_unexecuted_quantity() {
    ParentOrder parent = make_parent();
    parent.activate();
    parent.record_fill(20);
    parent.expire();
    CHECK(parent.status() == ParentOrderStatus::Expired);
    CHECK(parent.remaining_quantity() == 80);
}

void terminal_parent_rejects_further_transitions() {
    ParentOrder parent = make_parent();
    parent.cancel();
    EXPECT_THROW(parent.activate(), std::logic_error);
    EXPECT_THROW(parent.record_fill(1), std::logic_error);
    EXPECT_THROW(parent.expire(), std::logic_error);
    CHECK(!parent.can_generate_children());
}

void overfill_is_rejected_without_mutation() {
    ParentOrder parent = make_parent();
    parent.activate();
    EXPECT_THROW(parent.record_fill(101), std::invalid_argument);
    CHECK(parent.executed_quantity() == 0);
    CHECK(parent.remaining_quantity() == 100);
    CHECK(parent.status() == ParentOrderStatus::Active);
}

}  // namespace

int main() {
    valid_parent_starts_pending();
    zero_quantity_is_rejected();
    invalid_time_range_is_rejected();
    partial_execution_preserves_quantity_invariant();
    full_execution_is_terminal();
    cancellation_preserves_unexecuted_quantity();
    expiry_preserves_unexecuted_quantity();
    terminal_parent_rejects_further_transitions();
    overfill_is_rejected_without_mutation();
    return test_support::failures == 0 ? 0 : 1;
}
