#include "benchmark_scenarios.hpp"
#include "workload_generator.hpp"

#include <stdexcept>
#include <string>

namespace quant_engine::benchmarks {

BenchmarkScenario make_matching_scenario(const std::size_t operations,
                                         const std::size_t price_levels,
                                         const bool market_orders,
                                         const std::uint64_t seed) {
    return WorkloadGenerator{seed}.matching(operations, price_levels,
                                            market_orders);
}

BenchmarkScenario make_matching_walk_scenario(
    const std::size_t incoming_orders, const std::size_t fills_per_order,
    const std::uint64_t seed) {
    static_cast<void>(seed);
    if (incoming_orders == 0 || fills_per_order == 0) {
        throw std::invalid_argument("walk scenario sizes must be positive");
    }
    BenchmarkScenario scenario{
        "matching-level-walk", {}, {},
        "crossing LIMIT BUY; " + std::to_string(fills_per_order) +
            " fills and price levels consumed per incoming order"};
    const std::size_t resting_orders = incoming_orders * fills_per_order;
    scenario.setup.reserve(resting_orders);
    scenario.commands.reserve(incoming_orders);
    scenario.expected_active_orders = resting_orders;
    scenario.expected_completed_orders = resting_orders + incoming_orders;
    scenario.expected_trades = resting_orders;
    scenario.expected_orders_per_level = 1;
    for (std::size_t index = 0; index < resting_orders; ++index) {
        scenario.setup.push_back(BenchmarkCommand{
            BenchmarkOperationType::SubmitLimit,
            static_cast<OrderId>(index + 1), Side::Sell, 1,
            1'000'000 + static_cast<Price>(index), std::nullopt,
            std::nullopt});
    }
    for (std::size_t index = 0; index < incoming_orders; ++index) {
        scenario.commands.push_back(BenchmarkCommand{
            BenchmarkOperationType::SubmitLimit,
            static_cast<OrderId>(resting_orders + index + 1), Side::Buy,
            static_cast<Quantity>(fills_per_order), 3'000'000, std::nullopt,
            std::nullopt});
    }
    return scenario;
}

BenchmarkScenario make_market_exhaust_scenario(
    const std::size_t resting_orders, const std::uint64_t seed) {
    static_cast<void>(seed);
    if (resting_orders == 0) {
        throw std::invalid_argument("exhaust scenario size must be positive");
    }
    BenchmarkScenario scenario{
        "market-exhaust-book", {}, {},
        "one MARKET BUY exhausts all configured resting liquidity"};
    scenario.setup.reserve(resting_orders);
    scenario.expected_active_orders = resting_orders;
    scenario.expected_completed_orders = resting_orders + 1;
    scenario.expected_trades = resting_orders;
    scenario.expected_orders_per_level =
        (resting_orders + 99) / 100;
    for (std::size_t index = 0; index < resting_orders; ++index) {
        scenario.setup.push_back(BenchmarkCommand{
            BenchmarkOperationType::SubmitLimit,
            static_cast<OrderId>(index + 1), Side::Sell, 1,
            1'000'000 + static_cast<Price>(index % 100), std::nullopt,
            std::nullopt});
    }
    scenario.commands.push_back(BenchmarkCommand{
        BenchmarkOperationType::SubmitMarket,
        static_cast<OrderId>(resting_orders + 1), Side::Buy,
        static_cast<Quantity>(resting_orders), std::nullopt, std::nullopt,
        std::nullopt});
    return scenario;
}

}  // namespace quant_engine::benchmarks
