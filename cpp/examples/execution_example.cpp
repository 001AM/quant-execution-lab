#include "quant_engine/analytics/execution_analytics.hpp"
#include "quant_engine/execution/execution_scheduler.hpp"
#include "quant_engine/execution/twap.hpp"
#include "quant_engine/execution/vwap.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace quant_engine;
using namespace quant_engine::analytics;
using namespace quant_engine::execution;

struct Config {
    std::string symbol{"AAPL"};
    Quantity quantity{1'000};
    Price price_ticks{10'000};
};

Timestamp minute(const int value) {
    return Timestamp{} + std::chrono::minutes{value};
}

Quantity parse_quantity(const std::string& text) {
    std::size_t consumed = 0;
    const auto value = std::stoull(text, &consumed);
    if (consumed != text.size() || value < 4 || value > 10'000'000) {
        throw std::invalid_argument("quantity must be an integer from 4 to 10000000");
    }
    return static_cast<Quantity>(value);
}

Price parse_price(const std::string& text) {
    std::size_t consumed = 0;
    const auto value = std::stoll(text, &consumed);
    if (consumed != text.size() || value < 3 ||
        value >= std::numeric_limits<Price>::max()) {
        throw std::invalid_argument("price ticks must be an integer greater than 2");
    }
    return static_cast<Price>(value);
}

Config parse_args(const int argc, char** argv) {
    Config config;
    for (int index = 1; index < argc; index += 2) {
        if (index + 1 >= argc) {
            throw std::invalid_argument("every option requires a value");
        }
        const std::string option{argv[index]};
        const std::string value{argv[index + 1]};
        if (option == "--symbol") {
            if (value.empty() || value.size() > 30) {
                throw std::invalid_argument("symbol must contain 1 to 30 characters");
            }
            config.symbol = value;
        } else if (option == "--quantity") {
            config.quantity = parse_quantity(value);
        } else if (option == "--price-ticks") {
            config.price_ticks = parse_price(value);
        } else {
            throw std::invalid_argument("unknown option: " + option);
        }
    }
    return config;
}

ParentOrder parent(const Config& config) {
    return ParentOrder{1, config.symbol, Side::Buy, config.quantity, minute(0),
                       minute(4)};
}

void seed_liquidity(MatchingEngine& engine, const Config& config) {
    const auto result = engine.submit(OrderRequest{
        1, Side::Sell, OrderType::Limit, config.quantity, config.price_ticks,
        minute(0)});
    if (!result.accepted) {
        throw std::runtime_error("failed to seed deterministic liquidity");
    }
}

ExecutionSummary run_twap(const Config& config) {
    MatchingEngine engine;
    seed_liquidity(engine, config);
    ExecutionScheduler scheduler{engine};
    const ParentOrder order = parent(config);
    scheduler.add_parent(order, TWAP({4}).generate_schedule(order, {100, 1}),
                         "TWAP");
    static_cast<void>(scheduler.advance_to(minute(3)));
    return scheduler.summary(1);
}

ExecutionSummary run_vwap(const Config& config) {
    MatchingEngine engine;
    seed_liquidity(engine, config);
    ExecutionScheduler scheduler{engine};
    const ParentOrder order = parent(config);
    const VolumeProfile profile({
        {minute(0), minute(1), 0.10},
        {minute(1), minute(2), 0.20},
        {minute(2), minute(3), 0.30},
        {minute(3), minute(4), 0.40},
    });
    scheduler.add_parent(
        order, VWAP({profile}).generate_schedule(order, {100, 1}), "VWAP");
    static_cast<void>(scheduler.advance_to(minute(3)));
    return scheduler.summary(1);
}

ExecutionSummary run_pov(const Config& config) {
    MatchingEngine engine;
    seed_liquidity(engine, config);
    ExecutionScheduler scheduler{engine};
    scheduler.add_parent(parent(config), {}, "POV");
    const POV algorithm{{0.20}};
    const Quantity target_per_slice = (config.quantity + 3U) / 4U;
    const Quantity observed_volume = target_per_slice * 5U;
    for (int index = 0; index < 4; ++index) {
        static_cast<void>(scheduler.on_market_volume(
            1, algorithm,
            MarketVolumeObservation{minute(index), observed_volume},
            static_cast<OrderId>(100 + index),
            static_cast<SequenceNumber>(index + 1)));
    }
    return scheduler.summary(1);
}

void print_quality(const ExecutionSummary& summary, const Config& config) {
    const Quantity first_quantity = std::max<Quantity>(1, config.quantity / 4U);
    const Quantity second_quantity = std::max<Quantity>(1, config.quantity / 2U);
    const Quantity final_quantity =
        config.quantity - first_quantity - second_quantity;
    const ExecutionBenchmark benchmark{
        .arrival_price = config.price_ticks,
        .decision_price = std::max<Price>(1, config.price_ticks - 2),
        .final_market_price = config.price_ticks + 1,
        .arrival_bid = config.price_ticks - 1,
        .arrival_ask = config.price_ticks,
        .market_trades = {{config.price_ticks - 1, first_quantity, minute(0)},
                          {config.price_ticks, second_quantity, minute(1)},
                          {config.price_ticks + 1, final_quantity, minute(2)}},
        .fees = static_cast<double>(config.quantity) * 0.005,
        .tick_value = 0.01,
    };
    std::cout << summary.to_string() << '\n'
              << generate_execution_quality_report(summary, benchmark).to_string()
              << "\n\n";
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const Config config = parse_args(argc, argv);
        std::cout << "MARKET INPUT\n"
                  << "Symbol: " << config.symbol << '\n'
                  << "Quantity: " << config.quantity << '\n'
                  << "Reference price: " << std::fixed << std::setprecision(2)
                  << static_cast<double>(config.price_ticks) / 100.0 << '\n'
                  << "Tick size: 0.01 (engine report prices below are integer ticks)\n\n";
        print_quality(run_twap(config), config);
        print_quality(run_vwap(config), config);
        print_quality(run_pov(config), config);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "execution example: " << error.what() << '\n';
        return 2;
    }
}
