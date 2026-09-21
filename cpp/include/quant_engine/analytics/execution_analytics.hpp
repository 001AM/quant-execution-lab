#pragma once

#include "quant_engine/analytics/implementation_shortfall.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace quant_engine::analytics {

struct ExecutionQualityReport {
    ParentOrderId parent_order_id;
    Side side;
    Quantity requested_quantity;
    Quantity executed_quantity;
    Quantity unexecuted_quantity;
    Price arrival_price;
    std::optional<double> average_execution_price;
    std::optional<double> market_vwap;
    std::optional<Price> final_market_price;
    std::optional<double> arrival_slippage;
    std::optional<double> arrival_slippage_bps;
    std::optional<double> arrival_slippage_amount;
    std::optional<double> vwap_slippage;
    std::optional<double> vwap_slippage_bps;
    std::optional<double> vwap_slippage_amount;
    std::optional<double> estimated_spread_cost;
    double execution_cost;
    std::optional<double> opportunity_cost;
    std::optional<double> delay_cost;
    double fees;
    std::optional<double> implementation_shortfall;
    std::optional<Timestamp> first_fill_time;
    std::optional<Timestamp> last_fill_time;
    std::optional<std::chrono::nanoseconds> execution_duration;

    [[nodiscard]] std::string to_string() const;
};

[[nodiscard]] std::optional<double> weighted_average_execution_price(
    const std::vector<execution::ExecutionFill>& fills);
[[nodiscard]] std::optional<double> calculate_market_vwap(
    const std::vector<MarketTradeObservation>& market_trades);
[[nodiscard]] double signed_slippage(Side side, double execution_price,
                                     double benchmark_price);
[[nodiscard]] double slippage_bps(double signed_slippage_value,
                                  double benchmark_price);
[[nodiscard]] ExecutionQualityReport generate_execution_quality_report(
    const execution::ExecutionSummary& summary,
    const ExecutionBenchmark& benchmark);

}  // namespace quant_engine::analytics
