#pragma once

#include "benchmark_types.hpp"

#include "quant_engine/order_book.hpp"

#include <cstddef>
#include <cstdint>

namespace quant_engine::benchmarks {

struct BenchmarkRunConfig {
    std::uint64_t seed{42};
    std::size_t runs{5};
    bool record_latency{true};
    bool reserve_capacity{false};
};

[[nodiscard]] BenchmarkResult run_benchmark(
    const BenchmarkScenario& scenario, const BenchmarkRunConfig& config);
[[nodiscard]] std::uint64_t book_checksum(const OrderBook& book);

}  // namespace quant_engine::benchmarks
