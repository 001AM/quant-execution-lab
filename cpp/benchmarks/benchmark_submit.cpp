#include "benchmark_scenarios.hpp"
#include "workload_generator.hpp"

namespace quant_engine::benchmarks {

BenchmarkScenario make_submit_scenario(const std::size_t operations,
                                       const std::size_t price_levels,
                                       const std::uint64_t seed) {
    return WorkloadGenerator{seed}.submit(operations, price_levels);
}

}  // namespace quant_engine::benchmarks
