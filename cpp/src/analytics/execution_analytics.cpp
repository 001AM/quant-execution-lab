#include "quant_engine/analytics/execution_analytics.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace quant_engine::analytics {
namespace {

void validate_benchmark(const ExecutionBenchmark& benchmark) {
    if (benchmark.arrival_price <= 0) {
        throw std::invalid_argument("arrival price must be positive");
    }
    if (!std::isfinite(benchmark.tick_value) || benchmark.tick_value <= 0.0) {
        throw std::invalid_argument("tick value must be finite and positive");
    }
    if (!std::isfinite(benchmark.fees) || benchmark.fees < 0.0) {
        throw std::invalid_argument("fees must be finite and non-negative");
    }
    if (benchmark.final_market_price.has_value() &&
        *benchmark.final_market_price <= 0) {
        throw std::invalid_argument("final market price must be positive");
    }
    if (benchmark.arrival_bid.has_value() != benchmark.arrival_ask.has_value()) {
        throw std::invalid_argument(
            "arrival bid and ask must either both be present or both be absent");
    }
    if (benchmark.arrival_bid.has_value() &&
        (*benchmark.arrival_bid <= 0 || *benchmark.arrival_ask <= 0 ||
         *benchmark.arrival_bid > *benchmark.arrival_ask)) {
        throw std::invalid_argument("arrival bid/ask snapshot is invalid");
    }
}

std::string optional_number(const std::optional<double>& value) {
    if (!value.has_value()) {
        return "N/A";
    }
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(4) << *value;
    return stream.str();
}

}  // namespace

std::optional<double> weighted_average_execution_price(
    const std::vector<execution::ExecutionFill>& fills) {
    if (fills.empty()) {
        return std::nullopt;
    }

    long double weighted_value = 0.0L;
    Quantity total_quantity = 0;
    for (const auto& fill : fills) {
        if (fill.price <= 0 || fill.quantity == 0) {
            throw std::invalid_argument(
                "execution fills require positive price and quantity");
        }
        if (std::numeric_limits<Quantity>::max() - total_quantity <
            fill.quantity) {
            throw std::overflow_error("execution fill quantity overflow");
        }
        total_quantity += fill.quantity;
        weighted_value += static_cast<long double>(fill.price) *
                          static_cast<long double>(fill.quantity);
    }
    return static_cast<double>(weighted_value /
                               static_cast<long double>(total_quantity));
}

std::optional<double> calculate_market_vwap(
    const std::vector<MarketTradeObservation>& market_trades) {
    if (market_trades.empty()) {
        return std::nullopt;
    }

    long double weighted_value = 0.0L;
    Quantity total_volume = 0;
    for (const auto& trade : market_trades) {
        if (trade.price <= 0 || trade.volume == 0) {
            throw std::invalid_argument(
                "market observations require positive price and volume");
        }
        if (std::numeric_limits<Quantity>::max() - total_volume < trade.volume) {
            throw std::overflow_error("market volume overflow");
        }
        total_volume += trade.volume;
        weighted_value += static_cast<long double>(trade.price) *
                          static_cast<long double>(trade.volume);
    }
    return static_cast<double>(weighted_value /
                               static_cast<long double>(total_volume));
}

double signed_slippage(const Side side, const double execution_price,
                       const double benchmark_price) {
    if (!std::isfinite(execution_price) || execution_price <= 0.0 ||
        !std::isfinite(benchmark_price) || benchmark_price <= 0.0) {
        throw std::invalid_argument(
            "slippage prices must be finite and positive");
    }
    if (side != Side::Buy && side != Side::Sell) {
        throw std::invalid_argument("side must be Buy or Sell");
    }
    return side == Side::Buy ? execution_price - benchmark_price
                             : benchmark_price - execution_price;
}

double slippage_bps(const double signed_slippage_value,
                    const double benchmark_price) {
    if (!std::isfinite(signed_slippage_value) ||
        !std::isfinite(benchmark_price) || benchmark_price <= 0.0) {
        throw std::invalid_argument(
            "basis-point inputs must be finite and benchmark positive");
    }
    return signed_slippage_value / benchmark_price * 10'000.0;
}

ExecutionQualityReport generate_execution_quality_report(
    const execution::ExecutionSummary& summary,
    const ExecutionBenchmark& benchmark) {
    validate_benchmark(benchmark);
    const auto shortfall =
        calculate_implementation_shortfall(summary, benchmark);
    const auto market_vwap = calculate_market_vwap(benchmark.market_trades);

    std::optional<double> arrival_slippage;
    std::optional<double> arrival_slippage_basis_points;
    std::optional<double> arrival_slippage_amount;
    std::optional<double> vwap_slippage;
    std::optional<double> vwap_slippage_basis_points;
    std::optional<double> vwap_slippage_amount;
    std::optional<double> estimated_spread_cost;

    if (summary.average_execution_price.has_value()) {
        arrival_slippage =
            signed_slippage(summary.side, *summary.average_execution_price,
                            static_cast<double>(benchmark.arrival_price));
        arrival_slippage_basis_points =
            slippage_bps(*arrival_slippage,
                         static_cast<double>(benchmark.arrival_price));
        arrival_slippage_amount =
            *arrival_slippage * static_cast<double>(summary.executed_quantity) *
            benchmark.tick_value;

        if (market_vwap.has_value()) {
            vwap_slippage = signed_slippage(
                summary.side, *summary.average_execution_price, *market_vwap);
            vwap_slippage_basis_points =
                slippage_bps(*vwap_slippage, *market_vwap);
            vwap_slippage_amount =
                *vwap_slippage *
                static_cast<double>(summary.executed_quantity) *
                benchmark.tick_value;
        }

        if (benchmark.arrival_bid.has_value()) {
            const double midpoint =
                (static_cast<double>(*benchmark.arrival_bid) +
                 static_cast<double>(*benchmark.arrival_ask)) /
                2.0;
            estimated_spread_cost =
                signed_slippage(summary.side, *summary.average_execution_price,
                                midpoint) *
                static_cast<double>(summary.executed_quantity) *
                benchmark.tick_value;
        }
    }

    std::optional<std::chrono::nanoseconds> duration;
    if (summary.first_fill_time.has_value() &&
        summary.last_fill_time.has_value()) {
        if (*summary.last_fill_time < *summary.first_fill_time) {
            throw std::invalid_argument(
                "last fill time cannot precede first fill time");
        }
        duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
            *summary.last_fill_time - *summary.first_fill_time);
    } else if (summary.first_fill_time.has_value() !=
               summary.last_fill_time.has_value()) {
        throw std::invalid_argument(
            "first and last fill times must be present together");
    }

    return ExecutionQualityReport{
        .parent_order_id = summary.parent_order_id,
        .side = summary.side,
        .requested_quantity = summary.requested_quantity,
        .executed_quantity = summary.executed_quantity,
        .unexecuted_quantity = summary.remaining_quantity,
        .arrival_price = benchmark.arrival_price,
        .average_execution_price = summary.average_execution_price,
        .market_vwap = market_vwap,
        .final_market_price = benchmark.final_market_price,
        .arrival_slippage = arrival_slippage,
        .arrival_slippage_bps = arrival_slippage_basis_points,
        .arrival_slippage_amount = arrival_slippage_amount,
        .vwap_slippage = vwap_slippage,
        .vwap_slippage_bps = vwap_slippage_basis_points,
        .vwap_slippage_amount = vwap_slippage_amount,
        .estimated_spread_cost = estimated_spread_cost,
        .execution_cost = shortfall.execution_cost,
        .opportunity_cost = shortfall.opportunity_cost,
        .delay_cost = shortfall.delay_cost,
        .fees = shortfall.fees,
        .implementation_shortfall = shortfall.total,
        .first_fill_time = summary.first_fill_time,
        .last_fill_time = summary.last_fill_time,
        .execution_duration = duration,
    };
}

std::string ExecutionQualityReport::to_string() const {
    std::ostringstream stream;
    stream << "EXECUTION QUALITY\n"
           << "Parent Order: " << parent_order_id << '\n'
           << "Side: " << (side == Side::Buy ? "BUY" : "SELL") << '\n'
           << "Requested: " << requested_quantity << '\n'
           << "Executed: " << executed_quantity << '\n'
           << "Unexecuted: " << unexecuted_quantity << '\n'
           << "Arrival Price: " << arrival_price << '\n'
           << "Average Price: " << optional_number(average_execution_price)
           << '\n'
           << "Market VWAP: " << optional_number(market_vwap) << '\n'
           << "Arrival Slippage (bps): "
           << optional_number(arrival_slippage_bps) << '\n'
           << "VWAP Slippage (bps): " << optional_number(vwap_slippage_bps)
           << '\n'
           << "Execution Cost: " << execution_cost << '\n'
           << "Opportunity Cost: " << optional_number(opportunity_cost) << '\n'
           << "Fees: " << fees << '\n'
           << "Implementation Shortfall: "
           << optional_number(implementation_shortfall);
    return stream.str();
}

}  // namespace quant_engine::analytics
