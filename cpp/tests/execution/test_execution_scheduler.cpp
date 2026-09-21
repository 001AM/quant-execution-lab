#include "quant_engine/execution/execution_scheduler.hpp"
#include "quant_engine/execution/twap.hpp"
#include "quant_engine/execution/vwap.hpp"

#include "../test_support.hpp"

#include <chrono>
#include <cmath>
#include <stdexcept>

namespace {
using namespace quant_engine;
using namespace quant_engine::execution;

Timestamp minute(const int value) {
    return Timestamp{} + std::chrono::minutes{value};
}

ParentOrder parent(const ParentOrderId id, const Quantity quantity,
                   const int end_minute) {
    return ParentOrder{id, "AAPL", Side::Buy, quantity, minute(0),
                       minute(end_minute)};
}

void add_ask(MatchingEngine& engine, const OrderId id, const Price price,
             const Quantity quantity, const int at_minute = 0) {
    CHECK(engine.submit(OrderRequest{id, Side::Sell, OrderType::Limit, quantity,
                                     price, minute(at_minute)})
              .accepted);
}

VolumeProfile four_bucket_profile() {
    return VolumeProfile({
        {minute(0), minute(1), 0.10},
        {minute(1), minute(2), 0.20},
        {minute(2), minute(3), 0.30},
        {minute(3), minute(4), 0.40},
    });
}

void scheduled_child_is_submitted_at_simulated_time() {
    MatchingEngine engine;
    ExecutionScheduler scheduler{engine};
    const ParentOrder order = parent(1, 100, 2);
    const auto schedule = TWAP({2}).generate_schedule(order, {100, 1});
    scheduler.add_parent(order, schedule, "TWAP");
    CHECK(scheduler.advance_to(minute(0)).size() == 1);
    CHECK(scheduler.child_executions(1).size() == 1);
    CHECK(scheduler.current_time() == minute(0));
}

void same_timestamp_children_use_schedule_sequence() {
    MatchingEngine engine;
    add_ask(engine, 1, 100, 20);
    ExecutionScheduler scheduler{engine};
    ParentOrder first = parent(1, 10, 1);
    ParentOrder second = parent(2, 10, 1);
    ChildOrder later{100, 1, Side::Buy, 10, OrderType::Market, std::nullopt,
                     minute(0), 2};
    ChildOrder earlier{200, 2, Side::Buy, 10, OrderType::Market, std::nullopt,
                       minute(0), 1};
    scheduler.add_parent(first, {later}, "TEST");
    scheduler.add_parent(second, {earlier}, "TEST");
    const auto submitted = scheduler.advance_to(minute(0));
    CHECK(submitted.size() == 2);
    CHECK(submitted[0].child.order_id == 200);
    CHECK(submitted[1].child.order_id == 100);
}

void child_submission_flows_through_matching_engine() {
    MatchingEngine engine;
    add_ask(engine, 1, 100, 100);
    ExecutionScheduler scheduler{engine};
    const ParentOrder order = parent(1, 100, 1);
    scheduler.add_parent(order, TWAP({1}).generate_schedule(order, {100, 1}),
                         "TWAP");
    const auto submitted = scheduler.advance_to(minute(0));
    CHECK(submitted[0].result.trades.size() == 1);
    CHECK(submitted[0].result.trades[0].price == 100);
    CHECK(scheduler.parent(1)->status() == ParentOrderStatus::Filled);
}

void partial_fill_updates_parent_from_trades_not_submission() {
    MatchingEngine engine;
    add_ask(engine, 1, 100, 60);
    ExecutionScheduler scheduler{engine};
    const ParentOrder order = parent(1, 300, 3);
    scheduler.add_parent(order, TWAP({3}).generate_schedule(order, {100, 1}),
                         "TWAP");
    const auto submitted = scheduler.advance_to(minute(0));
    CHECK(submitted[0].child.quantity == 100);
    CHECK(submitted[0].result.report.executed_quantity == 60);
    CHECK(scheduler.parent(1)->executed_quantity() == 60);
    CHECK(scheduler.parent(1)->remaining_quantity() == 240);
}

void no_fill_leaves_parent_quantity_unchanged() {
    MatchingEngine engine;
    ExecutionScheduler scheduler{engine};
    const ParentOrder order = parent(1, 100, 1);
    scheduler.add_parent(order, TWAP({1}).generate_schedule(order, {100, 1}),
                         "TWAP");
    static_cast<void>(scheduler.advance_to(minute(0)));
    CHECK(scheduler.parent(1)->executed_quantity() == 0);
    CHECK(scheduler.parent(1)->remaining_quantity() == 100);
    CHECK(!scheduler.summary(1).average_execution_price.has_value());
}

void final_slice_catches_up_prior_unfilled_quantity() {
    MatchingEngine engine;
    ExecutionScheduler scheduler{engine};
    const ParentOrder order = parent(1, 300, 3);
    scheduler.add_parent(order, TWAP({3}).generate_schedule(order, {100, 1}),
                         "TWAP");
    add_ask(engine, 1, 100, 60);
    static_cast<void>(scheduler.advance_to(minute(0)));
    add_ask(engine, 2, 101, 100, 1);
    static_cast<void>(scheduler.advance_to(minute(1)));
    add_ask(engine, 3, 102, 140, 2);
    const auto final = scheduler.advance_to(minute(2));
    CHECK(final[0].child.quantity == 140);
    CHECK(scheduler.parent(1)->executed_quantity() == 300);
    CHECK(scheduler.parent(1)->status() == ParentOrderStatus::Filled);
}

void disabled_catch_up_expires_remaining_quantity() {
    MatchingEngine engine;
    ExecutionScheduler scheduler{engine, SchedulerConfig{false}};
    const ParentOrder order = parent(1, 300, 3);
    scheduler.add_parent(order, TWAP({3}).generate_schedule(order, {100, 1}),
                         "TWAP");
    add_ask(engine, 1, 100, 60);
    static_cast<void>(scheduler.advance_to(minute(0)));
    add_ask(engine, 2, 101, 100, 1);
    static_cast<void>(scheduler.advance_to(minute(1)));
    add_ask(engine, 3, 102, 100, 2);
    static_cast<void>(scheduler.advance_to(minute(2)));
    static_cast<void>(scheduler.advance_to(minute(3)));
    CHECK(scheduler.parent(1)->executed_quantity() == 260);
    CHECK(scheduler.parent(1)->remaining_quantity() == 40);
    CHECK(scheduler.parent(1)->status() == ParentOrderStatus::Expired);
}

void parent_cancellation_prevents_future_children() {
    MatchingEngine engine;
    ExecutionScheduler scheduler{engine};
    const ParentOrder order = parent(1, 300, 3);
    scheduler.add_parent(order, TWAP({3}).generate_schedule(order, {100, 1}),
                         "TWAP");
    static_cast<void>(scheduler.advance_to(minute(0)));
    CHECK(scheduler.cancel_parent(1));
    CHECK(scheduler.advance_to(minute(2)).empty());
    CHECK(scheduler.parent(1)->status() == ParentOrderStatus::Cancelled);
    CHECK(scheduler.child_executions(1).size() == 1);
}

void trade_maps_back_to_child_and_parent() {
    MatchingEngine engine;
    add_ask(engine, 1, 100, 100);
    ExecutionScheduler scheduler{engine};
    const ParentOrder order = parent(77, 100, 1);
    scheduler.add_parent(order, TWAP({1}).generate_schedule(order, {500, 1}),
                         "TWAP");
    const auto execution = scheduler.advance_to(minute(0));
    const TradeId trade_id = execution[0].result.trades[0].trade_id;
    const auto child = scheduler.child_for_trade(trade_id);
    CHECK(child.has_value());
    CHECK(child->order_id == 500);
    CHECK(child->parent_order_id == 77);
}

void full_twap_end_to_end_has_weighted_average_101() {
    MatchingEngine engine;
    ExecutionScheduler scheduler{engine};
    const ParentOrder order = parent(1, 300, 3);
    scheduler.add_parent(order, TWAP({3}).generate_schedule(order, {100, 1}),
                         "TWAP");
    add_ask(engine, 1, 100, 100);
    static_cast<void>(scheduler.advance_to(minute(0)));
    add_ask(engine, 2, 101, 100, 1);
    static_cast<void>(scheduler.advance_to(minute(1)));
    add_ask(engine, 3, 102, 100, 2);
    static_cast<void>(scheduler.advance_to(minute(2)));
    const auto summary = scheduler.summary(1);
    CHECK(summary.status == ParentOrderStatus::Filled);
    CHECK(summary.executed_quantity == 300);
    CHECK(summary.number_of_child_orders == 3);
    CHECK(summary.number_of_fills == 3);
    CHECK(std::abs(*summary.average_execution_price - 101.0) < 1e-12);
    CHECK(summary.first_fill_time == minute(0));
    CHECK(summary.last_fill_time == minute(2));
}

void full_vwap_end_to_end_uses_profile_quantities() {
    MatchingEngine engine;
    add_ask(engine, 1, 100, 1'000);
    ExecutionScheduler scheduler{engine};
    const ParentOrder order = parent(1, 1'000, 4);
    const auto schedule =
        VWAP({four_bucket_profile()}).generate_schedule(order, {100, 1});
    scheduler.add_parent(order, schedule, "VWAP");
    static_cast<void>(scheduler.advance_to(minute(3)));
    const auto& executions = scheduler.child_executions(1);
    CHECK(executions.size() == 4);
    CHECK(executions[0].child.quantity == 100);
    CHECK(executions[1].child.quantity == 200);
    CHECK(executions[2].child.quantity == 300);
    CHECK(executions[3].child.quantity == 400);
    CHECK(scheduler.summary(1).executed_quantity == 1'000);
    CHECK(scheduler.summary(1).status == ParentOrderStatus::Filled);
}

void full_pov_end_to_end_stops_at_parent_quantity() {
    MatchingEngine engine;
    add_ask(engine, 1, 100, 500);
    ExecutionScheduler scheduler{engine};
    scheduler.add_parent(parent(1, 500, 4), {}, "POV");
    const POV algorithm{{0.20}};
    const auto first = scheduler.on_market_volume(1, algorithm, {minute(0), 500}, 100, 1);
    const auto second = scheduler.on_market_volume(1, algorithm, {minute(1), 1'000}, 101, 2);
    const auto third = scheduler.on_market_volume(1, algorithm, {minute(2), 1'000}, 102, 3);
    CHECK(first->child.quantity == 100);
    CHECK(second->child.quantity == 200);
    CHECK(third->child.quantity == 200);
    CHECK(scheduler.parent(1)->status() == ParentOrderStatus::Filled);
    CHECK(!scheduler.on_market_volume(1, algorithm, {minute(3), 1'000}, 103, 4)
               .has_value());
    const auto summary = scheduler.summary(1);
    CHECK(summary.executed_quantity == 500);
    CHECK(summary.number_of_child_orders == 3);
    CHECK(std::abs(*summary.realized_participation - 0.20) < 1e-12);
}

void execution_summary_text_is_generated_from_real_state() {
    MatchingEngine engine;
    add_ask(engine, 1, 101, 100);
    ExecutionScheduler scheduler{engine};
    const ParentOrder order = parent(5, 100, 1);
    scheduler.add_parent(order, TWAP({1}).generate_schedule(order, {100, 1}),
                         "TWAP");
    static_cast<void>(scheduler.advance_to(minute(0)));
    const std::string report = scheduler.summary(5).to_string();
    CHECK(report.find("Parent Order:        5") != std::string::npos);
    CHECK(report.find("Executed Qty:        100") != std::string::npos);
    CHECK(report.find("Average Price:       101.0000") != std::string::npos);
    CHECK(report.find("FILLED") != std::string::npos);
}

void repeated_simulation_is_deterministic() {
    const auto run = []() {
        MatchingEngine engine;
        add_ask(engine, 1, 100, 300);
        ExecutionScheduler scheduler{engine};
        const ParentOrder order = parent(1, 300, 3);
        scheduler.add_parent(order, TWAP({3}).generate_schedule(order, {100, 1}),
                             "TWAP");
        static_cast<void>(scheduler.advance_to(minute(2)));
        return std::pair{scheduler.summary(1), engine.trades()};
    };
    const auto first = run();
    const auto second = run();
    CHECK(first.first.executed_quantity == second.first.executed_quantity);
    CHECK(first.first.average_execution_price == second.first.average_execution_price);
    CHECK(first.second == second.second);
}

void invalid_schedule_and_backward_clock_are_rejected() {
    MatchingEngine engine;
    ExecutionScheduler scheduler{engine};
    const ParentOrder order = parent(1, 10, 1);
    auto schedule = TWAP({1}).generate_schedule(order, {100, 1});
    schedule[0].quantity = 9;
    EXPECT_THROW(scheduler.add_parent(order, schedule, "TWAP"),
                 std::invalid_argument);

    ExecutionScheduler clock{engine};
    static_cast<void>(clock.advance_to(minute(2)));
    EXPECT_THROW(clock.advance_to(minute(1)), std::invalid_argument);
}

}  // namespace

int main() {
    scheduled_child_is_submitted_at_simulated_time();
    same_timestamp_children_use_schedule_sequence();
    child_submission_flows_through_matching_engine();
    partial_fill_updates_parent_from_trades_not_submission();
    no_fill_leaves_parent_quantity_unchanged();
    final_slice_catches_up_prior_unfilled_quantity();
    disabled_catch_up_expires_remaining_quantity();
    parent_cancellation_prevents_future_children();
    trade_maps_back_to_child_and_parent();
    full_twap_end_to_end_has_weighted_average_101();
    full_vwap_end_to_end_uses_profile_quantities();
    full_pov_end_to_end_stops_at_parent_quantity();
    execution_summary_text_is_generated_from_real_state();
    repeated_simulation_is_deterministic();
    invalid_schedule_and_backward_clock_are_rejected();
    return test_support::failures == 0 ? 0 : 1;
}
