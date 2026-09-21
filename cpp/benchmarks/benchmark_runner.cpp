#include "benchmark_runner.hpp"

#include "benchmark_report.hpp"
#include "latency_recorder.hpp"

#include "quant_engine/matching_engine.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <vector>

namespace quant_engine::benchmarks {
namespace {

using Clock = std::chrono::steady_clock;
volatile Price best_price_sink = 0;

struct CommandCounts {
    std::size_t trades{0};
    std::size_t fills{0};
};

CommandCounts apply_command(MatchingEngine& engine,
                            const BenchmarkCommand& command) {
    switch (command.type) {
        case BenchmarkOperationType::SubmitLimit:
        case BenchmarkOperationType::SubmitMarket: {
            const OrderType type =
                command.type == BenchmarkOperationType::SubmitLimit
                    ? OrderType::Limit
                    : OrderType::Market;
            auto result = engine.submit(OrderRequest{command.order_id,
                                                     command.side,
                                                     type,
                                                     command.quantity,
                                                     command.price});
            if (!result.accepted) {
                throw std::logic_error("generated submit command was rejected");
            }
            return {result.trades.size(), result.trades.size()};
        }
        case BenchmarkOperationType::Cancel: {
            const auto result = engine.cancel(command.order_id);
            if (!result.cancelled) {
                throw std::logic_error("generated cancel command was rejected");
            }
            return {};
        }
        case BenchmarkOperationType::Modify: {
            auto result = engine.modify(
                command.order_id,
                ModifyRequest{command.new_price, command.new_quantity});
            if (!result.modified) {
                throw std::logic_error("generated modify command was rejected");
            }
            return {result.trades.size(), result.trades.size()};
        }
        case BenchmarkOperationType::QueryBest: {
            best_price_sink = engine.book().best_bid().value_or(0) +
                              engine.book().best_ask().value_or(0);
            return {};
        }
    }
    throw std::logic_error("unknown benchmark operation");
}

void apply_setup(MatchingEngine& engine,
                 const std::vector<BenchmarkCommand>& setup) {
    for (const auto& command : setup) {
        static_cast<void>(apply_command(engine, command));
    }
}

struct CompletedRun {
    double elapsed_seconds;
    double operations_per_second;
    CommandCounts counts;
    std::size_t active_orders;
    std::size_t bid_levels;
    std::size_t ask_levels;
    std::uint64_t checksum;
    bool invariants_valid;
};

CompletedRun throughput_run(const BenchmarkScenario& scenario,
                            const bool reserve_capacity) {
    MatchingEngine engine;
    if (reserve_capacity) {
        engine.reserve_capacity(
            scenario.expected_active_orders,
            scenario.expected_completed_orders, scenario.expected_trades,
            scenario.expected_orders_per_level);
    }
    apply_setup(engine, scenario.setup);
    CommandCounts counts;
    const auto started = Clock::now();
    for (const auto& command : scenario.commands) {
        const auto current = apply_command(engine, command);
        counts.trades += current.trades;
        counts.fills += current.fills;
    }
    const auto finished = Clock::now();
    const double elapsed =
        std::chrono::duration<double>(finished - started).count();
    const auto snapshot = engine.book().snapshot();
    return CompletedRun{
        .elapsed_seconds = elapsed,
        .operations_per_second =
            static_cast<double>(scenario.commands.size()) / elapsed,
        .counts = counts,
        .active_orders = engine.book().active_order_count(),
        .bid_levels = snapshot.bids.size(),
        .ask_levels = snapshot.asks.size(),
        .checksum = book_checksum(engine.book()),
        .invariants_valid = engine.book().validate_invariants(),
    };
}

std::optional<LatencyStatistics> latency_run(
    const BenchmarkScenario& scenario, const bool reserve_capacity) {
    MatchingEngine engine;
    if (reserve_capacity) {
        engine.reserve_capacity(
            scenario.expected_active_orders,
            scenario.expected_completed_orders, scenario.expected_trades,
            scenario.expected_orders_per_level);
    }
    apply_setup(engine, scenario.setup);
    LatencyRecorder recorder;
    recorder.reserve(scenario.commands.size());
    for (const auto& command : scenario.commands) {
        const auto started = Clock::now();
        static_cast<void>(apply_command(engine, command));
        const auto finished = Clock::now();
        recorder.record(std::chrono::duration_cast<std::chrono::nanoseconds>(
            finished - started));
    }
    if (!engine.book().validate_invariants()) {
        throw std::logic_error("latency workload violated book invariants");
    }
    return recorder.statistics();
}

void warm_up(const BenchmarkScenario& scenario) {
    MatchingEngine engine;
    apply_setup(engine, scenario.setup);
    const std::size_t count =
        std::min<std::size_t>(10'000, scenario.commands.size());
    for (std::size_t index = 0; index < count; ++index) {
        static_cast<void>(apply_command(engine, scenario.commands[index]));
    }
}

}  // namespace

std::uint64_t book_checksum(const OrderBook& book) {
    constexpr std::uint64_t offset = 1'469'598'103'934'665'603ULL;
    constexpr std::uint64_t prime = 1'099'511'628'211ULL;
    std::uint64_t checksum = offset;
    const auto mix = [&](const std::uint64_t value) {
        checksum ^= value;
        checksum *= prime;
    };
    const auto snapshot = book.snapshot();
    mix(static_cast<std::uint64_t>(book.active_order_count()));
    mix(static_cast<std::uint64_t>(book.trades().size()));
    for (const auto& level : snapshot.bids) {
        mix(static_cast<std::uint64_t>(level.price));
        mix(level.total_quantity);
        mix(static_cast<std::uint64_t>(level.order_count));
    }
    for (const auto& level : snapshot.asks) {
        mix(static_cast<std::uint64_t>(level.price));
        mix(level.total_quantity);
        mix(static_cast<std::uint64_t>(level.order_count));
    }
    return checksum;
}

BenchmarkResult run_benchmark(const BenchmarkScenario& scenario,
                              const BenchmarkRunConfig& config) {
    if (scenario.commands.empty() || config.runs == 0) {
        throw std::invalid_argument(
            "benchmark requires commands and at least one run");
    }
    warm_up(scenario);
    std::vector<CompletedRun> completed;
    completed.reserve(config.runs);
    for (std::size_t run = 0; run < config.runs; ++run) {
        completed.push_back(throughput_run(scenario, config.reserve_capacity));
    }
    std::sort(completed.begin(), completed.end(),
              [](const CompletedRun& left, const CompletedRun& right) {
                  return left.operations_per_second < right.operations_per_second;
              });
    const CompletedRun& median = completed[completed.size() / 2];
    for (const auto& run : completed) {
        if (!run.invariants_valid || run.checksum != median.checksum ||
            run.counts.trades != median.counts.trades ||
            run.active_orders != median.active_orders) {
            throw std::logic_error(
                "repeated benchmark runs produced different engine state");
        }
    }

    return BenchmarkResult{
        .name = scenario.name,
        .timestamp_utc = current_timestamp_utc(),
        .operations = scenario.commands.size(),
        .elapsed_seconds = median.elapsed_seconds,
        .operations_per_second = median.operations_per_second,
        .latency = config.record_latency
                       ? latency_run(scenario, config.reserve_capacity)
                       : std::nullopt,
        .trades = median.counts.trades,
        .fills = median.counts.fills,
        .active_orders = median.active_orders,
        .bid_levels = median.bid_levels,
        .ask_levels = median.ask_levels,
        .checksum = median.checksum,
        .seed = config.seed,
        .runs = config.runs,
        .invariants_valid = median.invariants_valid,
        .composition = scenario.composition,
        .build_type = build_type_description(),
        .compiler = compiler_description(),
        .operating_system = operating_system_description(),
        .architecture = architecture_description(),
    };
}

}  // namespace quant_engine::benchmarks
