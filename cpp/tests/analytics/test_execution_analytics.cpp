#include "quant_engine/analytics/execution_analytics.hpp"

#include "../test_support.hpp"

#include <chrono>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {
using namespace quant_engine;
using namespace quant_engine::analytics;
using namespace quant_engine::execution;

Timestamp minute(const int value) {
    return Timestamp{} + std::chrono::minutes{value};
}

ExecutionSummary summary(const Side side, const Quantity requested,
                         const Quantity executed,
                         const std::optional<double> average) {
    return ExecutionSummary{.parent_order_id = 42,
                            .algorithm = "TEST",
                            .side = side,
                            .requested_quantity = requested,
                            .executed_quantity = executed,
                            .remaining_quantity = requested - executed,
                            .number_of_child_orders = 1,
                            .number_of_fills = executed == 0 ? 0U : 1U,
                            .average_execution_price = average,
                            .first_fill_time = executed == 0
                                                   ? std::nullopt
                                                   : std::optional{minute(1)},
                            .last_fill_time = executed == 0
                                                  ? std::nullopt
                                                  : std::optional{minute(3)},
                            .realized_participation = std::nullopt,
                            .status = executed == requested
                                          ? ParentOrderStatus::Filled
                                          : ParentOrderStatus::PartiallyFilled};
}

void weighted_average_uses_fill_quantity() {
    const std::vector<ExecutionFill> fills{{100, 100, minute(1)},
                                           {101, 200, minute(2)},
                                           {103, 100, minute(3)}};
    CHECK(std::abs(*weighted_average_execution_price(fills) - 101.25) <
          1e-12);
}

void empty_fills_have_no_average() {
    CHECK(!weighted_average_execution_price({}).has_value());
}

void invalid_fill_is_rejected() {
    EXPECT_THROW(weighted_average_execution_price({{100, 0, minute(1)}}),
                 std::invalid_argument);
}

void buy_arrival_slippage_is_positive_when_execution_is_higher() {
    CHECK(std::abs(signed_slippage(Side::Buy, 100.25, 100.0) - 0.25) <
          1e-12);
}

void sell_arrival_slippage_is_positive_when_execution_is_lower() {
    CHECK(std::abs(signed_slippage(Side::Sell, 99.75, 100.0) - 0.25) <
          1e-12);
}

void favorable_execution_has_negative_slippage() {
    CHECK(signed_slippage(Side::Buy, 99.5, 100.0) < 0.0);
    CHECK(signed_slippage(Side::Sell, 100.5, 100.0) < 0.0);
}

void slippage_converts_to_basis_points() {
    CHECK(std::abs(slippage_bps(0.25, 100.0) - 25.0) < 1e-12);
}

void market_vwap_uses_market_volume_not_fill_count() {
    const std::vector<MarketTradeObservation> market{{100, 100, minute(1)},
                                                      {101, 200, minute(2)},
                                                      {102, 100, minute(3)}};
    CHECK(std::abs(*calculate_market_vwap(market) - 101.0) < 1e-12);
}

void empty_market_tape_has_no_vwap() {
    CHECK(!calculate_market_vwap({}).has_value());
}

void buy_execution_vs_vwap_is_directional() {
    ExecutionBenchmark benchmark{.arrival_price = 100,
                                 .market_trades = {{100, 100, minute(1)},
                                                   {101, 100, minute(2)}}};
    const auto report = generate_execution_quality_report(
        summary(Side::Buy, 100, 100, 101.0), benchmark);
    CHECK(std::abs(*report.vwap_slippage - 0.5) < 1e-12);
    CHECK(std::abs(*report.vwap_slippage_amount - 50.0) < 1e-12);
}

void sell_execution_vs_vwap_is_directional() {
    ExecutionBenchmark benchmark{.arrival_price = 101,
                                 .market_trades = {{100, 100, minute(1)},
                                                   {101, 100, minute(2)}}};
    const auto report = generate_execution_quality_report(
        summary(Side::Sell, 100, 100, 100.0), benchmark);
    CHECK(std::abs(*report.vwap_slippage - 0.5) < 1e-12);
}

void slippage_amount_applies_tick_value() {
    const auto report = generate_execution_quality_report(
        summary(Side::Buy, 1'000, 1'000, 10'020.0),
        ExecutionBenchmark{.arrival_price = 10'000, .tick_value = 0.01});
    CHECK(std::abs(*report.arrival_slippage_amount - 200.0) < 1e-12);
    CHECK(std::abs(*report.arrival_slippage_bps - 20.0) < 1e-12);
}

void spread_cost_is_estimated_from_arrival_midpoint() {
    const auto report = generate_execution_quality_report(
        summary(Side::Buy, 100, 100, 101.0),
        ExecutionBenchmark{.arrival_price = 100,
                           .arrival_bid = 99,
                           .arrival_ask = 101});
    CHECK(std::abs(*report.estimated_spread_cost - 100.0) < 1e-12);
}

void execution_duration_uses_first_and_last_fill() {
    const auto report = generate_execution_quality_report(
        summary(Side::Buy, 100, 100, 100.0),
        ExecutionBenchmark{.arrival_price = 100});
    CHECK(report.execution_duration == std::chrono::minutes{2});
}

void invalid_arrival_price_is_rejected() {
    EXPECT_THROW(generate_execution_quality_report(
                     summary(Side::Buy, 100, 100, 100.0),
                     ExecutionBenchmark{.arrival_price = 0}),
                 std::invalid_argument);
}

void invalid_partial_quote_is_rejected() {
    EXPECT_THROW(generate_execution_quality_report(
                     summary(Side::Buy, 100, 100, 100.0),
                     ExecutionBenchmark{.arrival_price = 100,
                                        .arrival_bid = 99}),
                 std::invalid_argument);
}

void no_fill_leaves_execution_metrics_absent() {
    const auto report = generate_execution_quality_report(
        summary(Side::Buy, 100, 0, std::nullopt),
        ExecutionBenchmark{.arrival_price = 100,
                           .final_market_price = 105});
    CHECK(!report.average_execution_price.has_value());
    CHECK(!report.arrival_slippage.has_value());
    CHECK(!report.vwap_slippage.has_value());
    CHECK(report.opportunity_cost == 500.0);
}

void report_string_contains_calculated_metrics() {
    const auto report = generate_execution_quality_report(
        summary(Side::Buy, 100, 100, 101.0),
        ExecutionBenchmark{.arrival_price = 100});
    CHECK(report.to_string().find("EXECUTION QUALITY") != std::string::npos);
    CHECK(report.to_string().find("Arrival Slippage") != std::string::npos);
}

}  // namespace

int main() {
    weighted_average_uses_fill_quantity();
    empty_fills_have_no_average();
    invalid_fill_is_rejected();
    buy_arrival_slippage_is_positive_when_execution_is_higher();
    sell_arrival_slippage_is_positive_when_execution_is_lower();
    favorable_execution_has_negative_slippage();
    slippage_converts_to_basis_points();
    market_vwap_uses_market_volume_not_fill_count();
    empty_market_tape_has_no_vwap();
    buy_execution_vs_vwap_is_directional();
    sell_execution_vs_vwap_is_directional();
    slippage_amount_applies_tick_value();
    spread_cost_is_estimated_from_arrival_midpoint();
    execution_duration_uses_first_and_last_fill();
    invalid_arrival_price_is_rejected();
    invalid_partial_quote_is_rejected();
    no_fill_leaves_execution_metrics_absent();
    report_string_contains_calculated_metrics();
    return test_support::failures == 0 ? 0 : 1;
}
