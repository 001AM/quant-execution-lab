#include "benchmark_report.hpp"
#include "benchmark_runner.hpp"
#include "latency_recorder.hpp"
#include "workload_generator.hpp"

#include "../test_support.hpp"

#include "quant_engine/matching_engine.hpp"

#include <chrono>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {
using namespace quant_engine;
using namespace quant_engine::benchmarks;

void deterministic_generation_repeats_with_same_seed() {
    CHECK(WorkloadGenerator{42}.mixed(1'000).commands ==
          WorkloadGenerator{42}.mixed(1'000).commands);
}

void different_seed_changes_workload() {
    CHECK(WorkloadGenerator{42}.mixed(1'000).commands !=
          WorkloadGenerator{43}.mixed(1'000).commands);
}

void generated_submit_ids_are_nonzero_and_unique() {
    const auto commands = WorkloadGenerator{42}.submit(1'000, 10).commands;
    for (std::size_t index = 0; index < commands.size(); ++index) {
        CHECK(commands[index].order_id == index + 1);
    }
}

void percentile_uses_deterministic_nearest_rank() {
    const std::vector<double> values{1, 2, 3, 4, 5};
    CHECK(percentile_nearest_rank(values, 0.50) == 3.0);
    CHECK(percentile_nearest_rank(values, 0.95) == 5.0);
}

void empty_recorder_has_no_statistics() {
    CHECK(!LatencyRecorder{}.statistics().has_value());
}

void single_sample_populates_every_statistic() {
    LatencyRecorder recorder;
    recorder.record(std::chrono::nanoseconds{17});
    const auto stats = recorder.statistics();
    CHECK(stats->minimum_ns == 17.0);
    CHECK(stats->mean_ns == 17.0);
    CHECK(stats->p50_ns == 17.0);
    CHECK(stats->p95_ns == 17.0);
    CHECK(stats->p99_ns == 17.0);
    CHECK(stats->maximum_ns == 17.0);
}

LatencyStatistics ten_sample_statistics() {
    LatencyRecorder recorder;
    for (int value = 1; value <= 10; ++value) {
        recorder.record(std::chrono::nanoseconds{value});
    }
    return *recorder.statistics();
}

void p50_uses_nearest_rank() { CHECK(ten_sample_statistics().p50_ns == 5.0); }

void p95_uses_nearest_rank() { CHECK(ten_sample_statistics().p95_ns == 10.0); }

void p99_uses_nearest_rank() { CHECK(ten_sample_statistics().p99_ns == 10.0); }

void negative_latency_is_rejected() {
    LatencyRecorder recorder;
    EXPECT_THROW(recorder.record(std::chrono::nanoseconds{-1}),
                 std::invalid_argument);
}

void throughput_change_calculation_is_directional() {
    CHECK(std::abs(throughput_change_percent(100.0, 120.0) - 20.0) <
          1e-12);
    CHECK(std::abs(throughput_change_percent(100.0, 80.0) + 20.0) <
          1e-12);
}

void latency_change_calculation_is_directional() {
    CHECK(std::abs(latency_change_percent(100.0, 80.0) - 20.0) < 1e-12);
}

void benchmark_result_serializes_to_csv() {
    const BenchmarkResult result{
        .name = "test",
        .timestamp_utc = "2026-01-01T00:00:00Z",
        .operations = 10,
        .elapsed_seconds = 0.5,
        .operations_per_second = 20.0,
        .latency = LatencyStatistics{10, 1, 2, 2, 3, 4, 5},
        .trades = 1,
        .fills = 1,
        .active_orders = 9,
        .bid_levels = 1,
        .ask_levels = 0,
        .checksum = 123,
        .seed = 42,
        .runs = 5,
        .invariants_valid = true,
        .composition = "test",
        .build_type = "Release",
        .compiler = "compiler",
        .operating_system = "os",
        .architecture = "arch",
    };
    const auto csv = benchmark_result_to_csv(result);
    CHECK(csv.find("test,2026-01-01T00:00:00Z,10,0.5,20") == 0);
    CHECK(csv.find(",123,42,5,1,Release") != std::string::npos);
}

void checksum_is_deterministic_for_equivalent_books() {
    MatchingEngine first;
    MatchingEngine second;
    const OrderRequest request{1, Side::Buy, OrderType::Limit, 10, 100};
    CHECK(first.submit(request).accepted);
    CHECK(second.submit(request).accepted);
    CHECK(book_checksum(first.book()) == book_checksum(second.book()));
}

void generated_mixed_scenario_executes_without_rejections() {
    const auto scenario = WorkloadGenerator{42}.mixed(10'000);
    const auto result = run_benchmark(
        scenario, BenchmarkRunConfig{42, 1, false, false});
    CHECK(result.operations == 10'000);
    CHECK(result.invariants_valid);
    CHECK(result.checksum != 0);
}

}  // namespace

int main() {
    deterministic_generation_repeats_with_same_seed();
    different_seed_changes_workload();
    generated_submit_ids_are_nonzero_and_unique();
    percentile_uses_deterministic_nearest_rank();
    empty_recorder_has_no_statistics();
    single_sample_populates_every_statistic();
    p50_uses_nearest_rank();
    p95_uses_nearest_rank();
    p99_uses_nearest_rank();
    negative_latency_is_rejected();
    throughput_change_calculation_is_directional();
    latency_change_calculation_is_directional();
    benchmark_result_serializes_to_csv();
    checksum_is_deterministic_for_equivalent_books();
    generated_mixed_scenario_executes_without_rejections();
    return test_support::failures == 0 ? 0 : 1;
}
