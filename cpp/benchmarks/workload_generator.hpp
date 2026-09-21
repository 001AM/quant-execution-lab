#pragma once

#include "benchmark_types.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <string_view>

namespace quant_engine::benchmarks {

class WorkloadGenerator {
public:
    explicit WorkloadGenerator(std::uint64_t seed);

    [[nodiscard]] BenchmarkScenario submit(std::size_t operations,
                                           std::size_t price_levels);
    [[nodiscard]] BenchmarkScenario cancel(std::size_t operations,
                                           bool remove_levels);
    [[nodiscard]] BenchmarkScenario modify(std::size_t operations,
                                           std::string_view mode);
    [[nodiscard]] BenchmarkScenario matching(std::size_t operations,
                                             std::size_t price_levels,
                                             bool market_orders);
    [[nodiscard]] BenchmarkScenario mixed(std::size_t operations);

private:
    std::uint64_t seed_;
    std::mt19937_64 random_;
};

}  // namespace quant_engine::benchmarks
