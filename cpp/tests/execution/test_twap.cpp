#include "quant_engine/execution/twap.hpp"

#include "../test_support.hpp"

#include <chrono>
#include <numeric>
#include <stdexcept>

namespace {
using namespace quant_engine;
using namespace quant_engine::execution;

Timestamp minute(const int value) {
    return Timestamp{} + std::chrono::minutes{value};
}

ParentOrder parent(const Quantity quantity, const Side side = Side::Buy) {
    return ParentOrder{1, "AAPL", side, quantity, minute(0), minute(10)};
}

Quantity scheduled_total(const std::vector<ChildOrder>& schedule) {
    return std::accumulate(
        schedule.begin(), schedule.end(), Quantity{0},
        [](const Quantity total, const ChildOrder& child) {
            return total + child.quantity;
        });
}

void equal_slicing_allocates_exactly() {
    const auto schedule = TWAP({5}).generate_schedule(parent(1'000), {});
    CHECK(schedule.size() == 5);
    for (const ChildOrder& child : schedule) {
        CHECK(child.quantity == 200);
    }
}

void remainder_is_allocated_to_earliest_slices() {
    const auto schedule = TWAP({5}).generate_schedule(parent(1'003), {});
    CHECK(schedule[0].quantity == 201);
    CHECK(schedule[1].quantity == 201);
    CHECK(schedule[2].quantity == 201);
    CHECK(schedule[3].quantity == 200);
    CHECK(schedule[4].quantity == 200);
    CHECK(scheduled_total(schedule) == 1'003);
}

void timestamps_use_even_interval_starts_before_end() {
    const auto schedule = TWAP({5}).generate_schedule(parent(1'000), {});
    CHECK(schedule[0].scheduled_time == minute(0));
    CHECK(schedule[1].scheduled_time == minute(2));
    CHECK(schedule[2].scheduled_time == minute(4));
    CHECK(schedule[3].scheduled_time == minute(6));
    CHECK(schedule[4].scheduled_time == minute(8));
}

void total_quantity_invariant_holds_for_uneven_sizes() {
    for (const Quantity quantity : {Quantity{1}, Quantity{7}, Quantity{101},
                                    Quantity{10'007}}) {
        const auto schedule = TWAP({6}).generate_schedule(parent(quantity), {});
        CHECK(scheduled_total(schedule) == quantity);
        for (const ChildOrder& child : schedule) {
            CHECK(child.quantity > 0);
        }
    }
}

void one_slice_uses_start_time_and_full_quantity() {
    const auto schedule = TWAP({1}).generate_schedule(parent(99), {});
    CHECK(schedule.size() == 1);
    CHECK(schedule[0].quantity == 99);
    CHECK(schedule[0].scheduled_time == minute(0));
}

void slices_greater_than_quantity_skip_zero_children() {
    const auto schedule = TWAP({10}).generate_schedule(parent(3), {});
    CHECK(schedule.size() == 3);
    CHECK(schedule[0].quantity == 1);
    CHECK(schedule[1].quantity == 1);
    CHECK(schedule[2].quantity == 1);
}

void child_side_matches_buy_parent() {
    const auto schedule = TWAP({2}).generate_schedule(parent(10, Side::Buy), {});
    CHECK(schedule[0].side == Side::Buy);
    CHECK(schedule[1].side == Side::Buy);
}

void child_side_matches_sell_parent() {
    const auto schedule = TWAP({2}).generate_schedule(parent(10, Side::Sell), {});
    CHECK(schedule[0].side == Side::Sell);
    CHECK(schedule[1].side == Side::Sell);
}

void repeated_schedule_is_deterministic() {
    const TWAP algorithm{{5}};
    const ParentOrder order = parent(1'003);
    const ExecutionContext context{100, 200};
    CHECK(algorithm.generate_schedule(order, context) ==
          algorithm.generate_schedule(order, context));
}

void invalid_configuration_and_terminal_parent_are_handled() {
    EXPECT_THROW(TWAP(TWAPConfig{0}), std::invalid_argument);
    EXPECT_THROW(TWAP(TWAPConfig{5, OrderType::Limit}), std::invalid_argument);
    ParentOrder order = parent(10);
    order.cancel();
    CHECK(TWAP({2}).generate_schedule(order, {}).empty());
}

}  // namespace

int main() {
    equal_slicing_allocates_exactly();
    remainder_is_allocated_to_earliest_slices();
    timestamps_use_even_interval_starts_before_end();
    total_quantity_invariant_holds_for_uneven_sizes();
    one_slice_uses_start_time_and_full_quantity();
    slices_greater_than_quantity_skip_zero_children();
    child_side_matches_buy_parent();
    child_side_matches_sell_parent();
    repeated_schedule_is_deterministic();
    invalid_configuration_and_terminal_parent_are_handled();
    return test_support::failures == 0 ? 0 : 1;
}
