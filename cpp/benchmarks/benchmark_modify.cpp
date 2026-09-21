#include "benchmark_scenarios.hpp"
#include "workload_generator.hpp"

namespace quant_engine::benchmarks {

BenchmarkScenario make_modify_scenario(const std::size_t operations,
                                       const std::string& mode,
                                       const std::uint64_t seed) {
    return WorkloadGenerator{seed}.modify(operations, mode);
}

}  // namespace quant_engine::benchmarks
