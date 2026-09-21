#include "quant_engine/analytics/execution_analytics.hpp"
#include "quant_engine/execution/execution_scheduler.hpp"
#include "quant_engine/execution/twap.hpp"
#include "quant_engine/execution/vwap.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
using namespace quant_engine;
using namespace quant_engine::analytics;
using namespace quant_engine::execution;

Timestamp minute(const int value) {
    return Timestamp{} + std::chrono::minutes{value};
}

ParentOrder parent() {
    return ParentOrder{1, "AAPL", Side::Buy, 1'000, minute(0), minute(4)};
}

void seed_liquidity(MatchingEngine& engine) {
    const auto result = engine.submit(
        OrderRequest{1, Side::Sell, OrderType::Limit, 1'000, 100, minute(0)});
    if (!result.accepted) {
        throw std::runtime_error("failed to seed deterministic liquidity");
    }
}

ExecutionSummary run_twap() {
    MatchingEngine engine;
    seed_liquidity(engine);
    ExecutionScheduler scheduler{engine};
    const ParentOrder order = parent();
    scheduler.add_parent(order, TWAP({4}).generate_schedule(order, {100, 1}),
                         "TWAP");
    static_cast<void>(scheduler.advance_to(minute(3)));
    return scheduler.summary(1);
}

ExecutionSummary run_vwap() {
    MatchingEngine engine;
    seed_liquidity(engine);
    ExecutionScheduler scheduler{engine};
    const ParentOrder order = parent();
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

ExecutionSummary run_pov() {
    MatchingEngine engine;
    seed_liquidity(engine);
    ExecutionScheduler scheduler{engine};
    scheduler.add_parent(parent(), {}, "POV");
    const POV algorithm{{0.20}};
    for (int index = 0; index < 4; ++index) {
        static_cast<void>(scheduler.on_market_volume(
            1, algorithm, MarketVolumeObservation{minute(index), 1'250},
            static_cast<OrderId>(100 + index),
            static_cast<SequenceNumber>(index + 1)));
    }
    return scheduler.summary(1);
}

void print_quality(const ExecutionSummary& summary) {
    const ExecutionBenchmark benchmark{
        .arrival_price = 99,
        .decision_price = 98,
        .final_market_price = 101,
        .arrival_bid = 98,
        .arrival_ask = 100,
        .market_trades = {{99, 250, minute(0)},
                          {100, 500, minute(1)},
                          {101, 250, minute(2)}},
        .fees = 5.0,
        .tick_value = 1.0,
    };
    std::cout << summary.to_string() << '\n'
              << generate_execution_quality_report(summary, benchmark).to_string()
              << "\n\n";
}

}  // namespace

int main() {
    print_quality(run_twap());
    print_quality(run_vwap());
    print_quality(run_pov());
    return 0;
}
