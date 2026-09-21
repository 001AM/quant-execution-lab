#include "latency_recorder.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace quant_engine::benchmarks {

void LatencyRecorder::reserve(const std::size_t samples) {
    samples_.reserve(samples);
}

void LatencyRecorder::record(const std::chrono::nanoseconds latency) {
    if (latency.count() < 0) {
        throw std::invalid_argument("latency cannot be negative");
    }
    samples_.push_back(static_cast<double>(latency.count()));
}

double percentile_nearest_rank(const std::vector<double>& sorted_samples,
                               const double percentile) {
    if (sorted_samples.empty()) {
        throw std::invalid_argument("percentile requires at least one sample");
    }
    if (!std::isfinite(percentile) || percentile <= 0.0 ||
        percentile > 1.0) {
        throw std::invalid_argument("percentile must be in (0, 1]");
    }
    const auto rank = static_cast<std::size_t>(
        std::ceil(percentile * static_cast<double>(sorted_samples.size())));
    return sorted_samples[rank - 1];
}

std::optional<LatencyStatistics> LatencyRecorder::statistics() const {
    if (samples_.empty()) {
        return std::nullopt;
    }
    std::vector<double> sorted = samples_;
    std::sort(sorted.begin(), sorted.end());
    const double total = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    return LatencyStatistics{
        .samples = sorted.size(),
        .minimum_ns = sorted.front(),
        .mean_ns = total / static_cast<double>(sorted.size()),
        .p50_ns = percentile_nearest_rank(sorted, 0.50),
        .p95_ns = percentile_nearest_rank(sorted, 0.95),
        .p99_ns = percentile_nearest_rank(sorted, 0.99),
        .maximum_ns = sorted.back(),
    };
}

}  // namespace quant_engine::benchmarks
