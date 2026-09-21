#include "quant_engine/analytics/execution_analytics.hpp"

#include "../test_support.hpp"

#include <cmath>
#include <optional>
#include <stdexcept>

namespace {
using namespace quant_engine;
using namespace quant_engine::analytics;
using namespace quant_engine::execution;

ExecutionSummary summary(const Side side, const Quantity requested,
                         const Quantity executed, const double average) {
    return ExecutionSummary{.parent_order_id = 7,
                            .algorithm = "TEST",
                            .side = side,
                            .requested_quantity = requested,
                            .executed_quantity = executed,
                            .remaining_quantity = requested - executed,
                            .number_of_child_orders = 1,
                            .number_of_fills = executed == 0 ? 0U : 1U,
                            .average_execution_price = executed == 0
                                                           ? std::nullopt
                                                           : std::optional{average},
                            .first_fill_time = std::nullopt,
                            .last_fill_time = std::nullopt,
                            .realized_participation = std::nullopt,
                            .status = ParentOrderStatus::PartiallyFilled};
}

void complete_fill_shortfall_is_execution_cost() {
    const auto result = calculate_implementation_shortfall(
        summary(Side::Buy, 1'000, 1'000, 101.0),
        ExecutionBenchmark{.arrival_price = 100});
    CHECK(result.execution_cost == 1'000.0);
    CHECK(result.opportunity_cost == 0.0);
    CHECK(result.total == 1'000.0);
}

void partial_fill_acceptance_example_equals_1400() {
    const auto result = calculate_implementation_shortfall(
        summary(Side::Buy, 1'000, 800, 101.0),
        ExecutionBenchmark{.arrival_price = 100,
                           .final_market_price = 103});
    CHECK(result.execution_cost == 800.0);
    CHECK(result.opportunity_cost == 600.0);
    CHECK(result.total == 1'400.0);
}

void sell_opportunity_cost_uses_sell_direction() {
    const auto result = calculate_implementation_shortfall(
        summary(Side::Sell, 1'000, 800, 99.0),
        ExecutionBenchmark{.arrival_price = 100,
                           .final_market_price = 97});
    CHECK(result.execution_cost == 800.0);
    CHECK(result.opportunity_cost == 600.0);
}

void fees_are_separate_and_included_in_total() {
    const auto result = calculate_implementation_shortfall(
        summary(Side::Buy, 100, 100, 101.0),
        ExecutionBenchmark{.arrival_price = 100, .fees = 5.0});
    CHECK(result.execution_cost == 100.0);
    CHECK(result.fees == 5.0);
    CHECK(result.total == 105.0);
}

void zero_fees_do_not_change_shortfall() {
    const auto result = calculate_implementation_shortfall(
        summary(Side::Buy, 100, 100, 101.0),
        ExecutionBenchmark{.arrival_price = 100});
    CHECK(result.total == 100.0);
}

void buy_delay_cost_uses_decision_to_arrival_move() {
    const auto result = calculate_implementation_shortfall(
        summary(Side::Buy, 100, 100, 102.0),
        ExecutionBenchmark{.arrival_price = 101, .decision_price = 100});
    CHECK(result.delay_cost == 100.0);
    CHECK(result.total == 100.0);
}

void sell_delay_cost_uses_sell_direction() {
    const auto result = calculate_implementation_shortfall(
        summary(Side::Sell, 100, 100, 98.0),
        ExecutionBenchmark{.arrival_price = 99, .decision_price = 100});
    CHECK(result.delay_cost == 100.0);
}

void missing_decision_price_has_no_delay_cost() {
    const auto result = calculate_implementation_shortfall(
        summary(Side::Buy, 100, 100, 100.0),
        ExecutionBenchmark{.arrival_price = 100});
    CHECK(!result.delay_cost.has_value());
}

void missing_final_price_makes_partial_total_unavailable() {
    const auto result = calculate_implementation_shortfall(
        summary(Side::Buy, 100, 50, 101.0),
        ExecutionBenchmark{.arrival_price = 100});
    CHECK(!result.opportunity_cost.has_value());
    CHECK(!result.total.has_value());
}

void invalid_summary_invariant_is_rejected() {
    auto invalid = summary(Side::Buy, 100, 50, 101.0);
    invalid.remaining_quantity = 40;
    EXPECT_THROW(calculate_implementation_shortfall(
                     invalid, ExecutionBenchmark{.arrival_price = 100}),
                 std::invalid_argument);
}

void algorithm_reports_share_the_same_analytics_path() {
    for (const char* algorithm : {"TWAP", "VWAP", "POV"}) {
        auto input = summary(Side::Buy, 100, 100, 100.25);
        input.algorithm = algorithm;
        const auto report = generate_execution_quality_report(
            input, ExecutionBenchmark{.arrival_price = 100,
                                      .market_trades = {{100, 100, {}}}});
        CHECK(std::abs(*report.arrival_slippage_bps - 25.0) < 1e-12);
        CHECK(report.executed_quantity == 100);
    }
}

}  // namespace

int main() {
    complete_fill_shortfall_is_execution_cost();
    partial_fill_acceptance_example_equals_1400();
    sell_opportunity_cost_uses_sell_direction();
    fees_are_separate_and_included_in_total();
    zero_fees_do_not_change_shortfall();
    buy_delay_cost_uses_decision_to_arrival_move();
    sell_delay_cost_uses_sell_direction();
    missing_decision_price_has_no_delay_cost();
    missing_final_price_makes_partial_total_unavailable();
    invalid_summary_invariant_is_rejected();
    algorithm_reports_share_the_same_analytics_path();
    return test_support::failures == 0 ? 0 : 1;
}
