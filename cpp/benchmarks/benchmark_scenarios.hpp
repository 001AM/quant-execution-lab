#pragma once

#include "benchmark_types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace quant_engine::benchmarks {

[[nodiscard]] BenchmarkScenario make_submit_scenario(std::size_t operations,
                                                     std::size_t price_levels,
                                                     std::uint64_t seed);
[[nodiscard]] BenchmarkScenario make_cancel_scenario(std::size_t operations,
                                                     bool remove_levels,
                                                     std::uint64_t seed);
[[nodiscard]] BenchmarkScenario make_modify_scenario(std::size_t operations,
                                                     const std::string& mode,
                                                     std::uint64_t seed);
[[nodiscard]] BenchmarkScenario make_matching_scenario(
    std::size_t operations, std::size_t price_levels, bool market_orders,
    std::uint64_t seed);
[[nodiscard]] BenchmarkScenario make_matching_walk_scenario(
    std::size_t incoming_orders, std::size_t fills_per_order,
    std::uint64_t seed);
[[nodiscard]] BenchmarkScenario make_market_exhaust_scenario(
    std::size_t resting_orders, std::uint64_t seed);
[[nodiscard]] BenchmarkScenario make_mixed_scenario(std::size_t operations,
                                                    std::uint64_t seed);
[[nodiscard]] std::vector<std::size_t> standard_scaling_sizes();
[[nodiscard]] BenchmarkScenario make_best_price_scenario(
    std::size_t operations, std::size_t price_levels, std::uint64_t seed);

}  // namespace quant_engine::benchmarks
