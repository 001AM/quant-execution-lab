#include "quant_engine/analytics/implementation_shortfall.hpp"

#include <cmath>
#include <stdexcept>

namespace quant_engine::analytics {
namespace {

double directional_difference(const Side side, const double value,
                              const double reference) {
    if (side != Side::Buy && side != Side::Sell) {
        throw std::invalid_argument("side must be Buy or Sell");
    }
    return side == Side::Buy ? value - reference : reference - value;
}

void validate_inputs(const execution::ExecutionSummary& summary,
                     const ExecutionBenchmark& benchmark) {
    if (benchmark.arrival_price <= 0) {
        throw std::invalid_argument("arrival price must be positive");
    }
    if (!std::isfinite(benchmark.tick_value) || benchmark.tick_value <= 0.0) {
        throw std::invalid_argument("tick value must be finite and positive");
    }
    if (!std::isfinite(benchmark.fees) || benchmark.fees < 0.0) {
        throw std::invalid_argument("fees must be finite and non-negative");
    }
    if (summary.executed_quantity > summary.requested_quantity ||
        summary.remaining_quantity !=
            summary.requested_quantity - summary.executed_quantity) {
        throw std::invalid_argument(
            "execution summary quantities do not satisfy the parent invariant");
    }
    if (summary.executed_quantity > 0) {
        if (!summary.average_execution_price.has_value() ||
            !std::isfinite(*summary.average_execution_price) ||
            *summary.average_execution_price <= 0.0) {
            throw std::invalid_argument(
                "executed quantity requires a positive average execution price");
        }
    } else if (summary.average_execution_price.has_value()) {
        throw std::invalid_argument(
            "an average execution price is invalid when nothing executed");
    }
    if (benchmark.final_market_price.has_value() &&
        *benchmark.final_market_price <= 0) {
        throw std::invalid_argument("final market price must be positive");
    }
}

}  // namespace

ImplementationShortfall calculate_implementation_shortfall(
    const execution::ExecutionSummary& summary,
    const ExecutionBenchmark& benchmark) {
    validate_inputs(summary, benchmark);

    double execution_cost = 0.0;
    if (summary.average_execution_price.has_value()) {
        execution_cost =
            directional_difference(summary.side, *summary.average_execution_price,
                                   static_cast<double>(benchmark.arrival_price)) *
            static_cast<double>(summary.executed_quantity) * benchmark.tick_value;
    }

    std::optional<double> opportunity_cost;
    if (summary.remaining_quantity == 0) {
        opportunity_cost = 0.0;
    } else if (benchmark.final_market_price.has_value()) {
        opportunity_cost =
            directional_difference(summary.side,
                                   static_cast<double>(*benchmark.final_market_price),
                                   static_cast<double>(benchmark.arrival_price)) *
            static_cast<double>(summary.remaining_quantity) * benchmark.tick_value;
    }

    std::optional<double> delay_cost;
    if (benchmark.decision_price.has_value()) {
        if (*benchmark.decision_price <= 0) {
            throw std::invalid_argument("decision price must be positive");
        }
        delay_cost =
            directional_difference(summary.side,
                                   static_cast<double>(benchmark.arrival_price),
                                   static_cast<double>(*benchmark.decision_price)) *
            static_cast<double>(summary.requested_quantity) * benchmark.tick_value;
    }

    std::optional<double> total;
    if (opportunity_cost.has_value()) {
        total = execution_cost + benchmark.fees + *opportunity_cost;
    }

    return ImplementationShortfall{
        .execution_cost = execution_cost,
        .opportunity_cost = opportunity_cost,
        .delay_cost = delay_cost,
        .fees = benchmark.fees,
        .total = total,
    };
}

}  // namespace quant_engine::analytics
