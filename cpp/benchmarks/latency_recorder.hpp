#pragma once

#include "benchmark_types.hpp"

#include <chrono>
#include <optional>
#include <vector>

namespace quant_engine::benchmarks {

class LatencyRecorder {
public:
    void reserve(std::size_t samples);
    void record(std::chrono::nanoseconds latency);
    [[nodiscard]] std::size_t size() const noexcept { return samples_.size(); }
    [[nodiscard]] std::optional<LatencyStatistics> statistics() const;

private:
    std::vector<double> samples_;
};

[[nodiscard]] double percentile_nearest_rank(
    const std::vector<double>& sorted_samples, double percentile);

}  // namespace quant_engine::benchmarks
