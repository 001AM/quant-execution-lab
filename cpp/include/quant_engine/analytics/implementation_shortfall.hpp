#pragma once

#include "quant_engine/analytics/execution_benchmark.hpp"
#include "quant_engine/execution/execution_summary.hpp"

#include <optional>

namespace quant_engine::analytics {

struct ImplementationShortfall {
    double execution_cost;
    std::optional<double> opportunity_cost;
    std::optional<double> delay_cost;
    double fees;
    std::optional<double> total;
};

[[nodiscard]] ImplementationShortfall calculate_implementation_shortfall(
    const execution::ExecutionSummary& summary,
    const ExecutionBenchmark& benchmark);

}  // namespace quant_engine::analytics
