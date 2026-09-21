#include "quant_engine/execution/execution_scheduler.hpp"

#include "../test_support.hpp"

#include <chrono>
#include <stdexcept>

namespace {
using namespace quant_engine;
using namespace quant_engine::execution;

Timestamp minute(const int value) {
    return Timestamp{} + std::chrono::minutes{value};
}

ParentOrder parent(const Quantity quantity = 1'000) {
    return ParentOrder{1, "AAPL", Side::Buy, quantity, minute(0), minute(10)};
}

void basic_participation_uses_observed_market_volume() {
    const auto child = POV({0.10}).generate_child(
        parent(), MarketVolumeObservation{minute(1), 1'000}, 100, 1);
    CHECK(child.has_value());
    CHECK(child->quantity == 100);
}

void multiple_observations_produce_expected_independent_targets() {
    const POV algorithm{{0.10}};
    const ParentOrder order = parent();
    CHECK(algorithm.generate_child(order, {minute(1), 1'000}, 101, 1)->quantity == 100);
    CHECK(algorithm.generate_child(order, {minute(2), 2'000}, 102, 2)->quantity == 200);
    CHECK(algorithm.generate_child(order, {minute(3), 500}, 103, 3)->quantity == 50);
    CHECK(algorithm.generate_child(order, {minute(4), 1'500}, 104, 4)->quantity == 150);
}

void zero_market_volume_generates_no_child() {
    CHECK(!POV({0.10})
               .generate_child(parent(), {minute(1), 0}, 100, 1)
               .has_value());
}

void target_is_capped_by_parent_remaining_quantity() {
    ParentOrder order = parent(250);
    order.activate();
    order.record_fill(100);
    const auto child =
        POV({0.10}).generate_child(order, {minute(2), 2'000}, 101, 2);
    CHECK(child->quantity == 150);
}

void completed_parent_generates_no_further_children() {
    ParentOrder order = parent(100);
    order.activate();
    order.record_fill(100);
    CHECK(!POV({0.50})
               .generate_child(order, {minute(2), 1'000}, 101, 2)
               .has_value());
}

void invalid_participation_rates_are_rejected() {
    EXPECT_THROW(POV(POVConfig{0.0}), std::invalid_argument);
    EXPECT_THROW(POV(POVConfig{-0.1}), std::invalid_argument);
    EXPECT_THROW(POV(POVConfig{1.01}), std::invalid_argument);
}

void fractional_target_uses_deterministic_floor() {
    const auto child =
        POV({0.15}).generate_child(parent(), {minute(1), 11}, 100, 1);
    CHECK(child->quantity == 1);
    CHECK(!POV({0.01})
               .generate_child(parent(), {minute(1), 50}, 101, 2)
               .has_value());
}

void child_metadata_is_traceable_and_side_correct() {
    ParentOrder sell{7, "MSFT", Side::Sell, 100, minute(0), minute(10)};
    const auto child = POV({0.20}).generate_child(
        sell, MarketVolumeObservation{minute(3), 100}, 500, 42);
    CHECK(child->order_id == 500);
    CHECK(child->parent_order_id == 7);
    CHECK(child->side == Side::Sell);
    CHECK(child->scheduled_time == minute(3));
    CHECK(child->schedule_sequence == 42);
}

void partial_child_fill_updates_parent_by_actual_fill_only() {
    MatchingEngine engine;
    CHECK(engine.submit({1, Side::Sell, OrderType::Limit, 60, 100}).accepted);
    ExecutionScheduler scheduler{engine};
    scheduler.add_parent(parent(300), {}, "POV");
    const auto execution = scheduler.on_market_volume(
        1, POV({0.10}), MarketVolumeObservation{minute(1), 1'000}, 100, 1);
    CHECK(execution.has_value());
    CHECK(execution->child.quantity == 100);
    CHECK(execution->result.report.executed_quantity == 60);
    CHECK(scheduler.parent(1)->executed_quantity() == 60);
    CHECK(scheduler.parent(1)->remaining_quantity() == 240);
}

}  // namespace

int main() {
    basic_participation_uses_observed_market_volume();
    multiple_observations_produce_expected_independent_targets();
    zero_market_volume_generates_no_child();
    target_is_capped_by_parent_remaining_quantity();
    completed_parent_generates_no_further_children();
    invalid_participation_rates_are_rejected();
    fractional_target_uses_deterministic_floor();
    child_metadata_is_traceable_and_side_correct();
    partial_child_fill_updates_parent_by_actual_fill_only();
    return test_support::failures == 0 ? 0 : 1;
}
