#include "benchmark_scenarios.hpp"

#include <stdexcept>
#include <string>

namespace quant_engine::benchmarks {

std::vector<std::size_t> standard_scaling_sizes() {
    return {10'000, 100'000, 1'000'000};
}

BenchmarkScenario make_best_price_scenario(const std::size_t operations,
                                           const std::size_t price_levels,
                                           const std::uint64_t seed) {
    static_cast<void>(seed);
    if (operations == 0 || price_levels == 0) {
        throw std::invalid_argument("best-price scenario sizes must be positive");
    }
    BenchmarkScenario scenario{
        "best-price-access", {}, {},
        "read best bid and ask with " + std::to_string(price_levels) +
            " levels"};
    scenario.setup.reserve(price_levels * 2);
    scenario.commands.reserve(operations);
    scenario.expected_active_orders = price_levels * 2;
    scenario.expected_orders_per_level = 1;
    for (std::size_t index = 0; index < price_levels; ++index) {
        scenario.setup.push_back(BenchmarkCommand{
            BenchmarkOperationType::SubmitLimit,
            static_cast<OrderId>(index + 1), Side::Buy, 1,
            1'000'000 - static_cast<Price>(index), std::nullopt,
            std::nullopt});
        scenario.setup.push_back(BenchmarkCommand{
            BenchmarkOperationType::SubmitLimit,
            static_cast<OrderId>(price_levels + index + 1), Side::Sell, 1,
            1'001'000 + static_cast<Price>(index), std::nullopt,
            std::nullopt});
    }
    for (std::size_t index = 0; index < operations; ++index) {
        scenario.commands.push_back(BenchmarkCommand{
            BenchmarkOperationType::QueryBest, 0, Side::Buy, 0, std::nullopt,
            std::nullopt, std::nullopt});
    }
    return scenario;
}

}  // namespace quant_engine::benchmarks
