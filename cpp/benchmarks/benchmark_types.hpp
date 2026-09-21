#pragma once

#include "quant_engine/result.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace quant_engine::benchmarks {

enum class BenchmarkOperationType : std::uint8_t {
    SubmitLimit,
    SubmitMarket,
    Cancel,
    Modify,
    QueryBest,
};

struct BenchmarkCommand {
    BenchmarkOperationType type;
    OrderId order_id;
    Side side{Side::Buy};
    Quantity quantity{0};
    std::optional<Price> price;
    std::optional<Price> new_price;
    std::optional<Quantity> new_quantity;

    bool operator==(const BenchmarkCommand&) const = default;
};

struct BenchmarkScenario {
    std::string name;
    std::vector<BenchmarkCommand> setup;
    std::vector<BenchmarkCommand> commands;
    std::string composition;
    std::size_t expected_active_orders{0};
    std::size_t expected_completed_orders{0};
    std::size_t expected_trades{0};
    std::size_t expected_orders_per_level{0};
};

struct LatencyStatistics {
    std::size_t samples;
    double minimum_ns;
    double mean_ns;
    double p50_ns;
    double p95_ns;
    double p99_ns;
    double maximum_ns;
};

struct BenchmarkResult {
    std::string name;
    std::string timestamp_utc;
    std::size_t operations;
    double elapsed_seconds;
    double operations_per_second;
    std::optional<LatencyStatistics> latency;
    std::size_t trades;
    std::size_t fills;
    std::size_t active_orders;
    std::size_t bid_levels;
    std::size_t ask_levels;
    std::uint64_t checksum;
    std::uint64_t seed;
    std::size_t runs;
    bool invariants_valid;
    std::string composition;
    std::string build_type;
    std::string compiler;
    std::string operating_system;
    std::string architecture;
};

}  // namespace quant_engine::benchmarks
