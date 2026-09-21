#pragma once

#include "benchmark_types.hpp"

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace quant_engine::benchmarks {

[[nodiscard]] std::string compiler_description();
[[nodiscard]] std::string operating_system_description();
[[nodiscard]] std::string architecture_description();
[[nodiscard]] std::string build_type_description();
[[nodiscard]] std::string current_timestamp_utc();
[[nodiscard]] std::string benchmark_result_csv_header();
[[nodiscard]] std::string benchmark_result_to_csv(const BenchmarkResult& result);
void write_csv_report(const std::filesystem::path& output,
                      const std::vector<BenchmarkResult>& results);
void print_report(std::ostream& output, const BenchmarkResult& result);
[[nodiscard]] double throughput_change_percent(double baseline,
                                               double optimized);
[[nodiscard]] double latency_change_percent(double baseline,
                                            double optimized);

}  // namespace quant_engine::benchmarks
