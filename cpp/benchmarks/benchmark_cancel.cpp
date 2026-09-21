#include "benchmark_scenarios.hpp"
#include "workload_generator.hpp"

namespace quant_engine::benchmarks {

BenchmarkScenario make_cancel_scenario(const std::size_t operations,
                                       const bool remove_levels,
                                       const std::uint64_t seed) {
    return WorkloadGenerator{seed}.cancel(operations, remove_levels);
}

}  // namespace quant_engine::benchmarks
