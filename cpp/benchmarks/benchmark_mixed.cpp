#include "benchmark_scenarios.hpp"
#include "workload_generator.hpp"

namespace quant_engine::benchmarks {

BenchmarkScenario make_mixed_scenario(const std::size_t operations,
                                      const std::uint64_t seed) {
    return WorkloadGenerator{seed}.mixed(operations);
}

}  // namespace quant_engine::benchmarks
