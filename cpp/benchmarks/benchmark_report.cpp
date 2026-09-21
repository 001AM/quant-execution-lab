#include "benchmark_report.hpp"

#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace quant_engine::benchmarks {

std::string compiler_description() {
#if defined(__clang__)
    return "Clang " __clang_version__;
#elif defined(__GNUC__)
    return "GCC " __VERSION__;
#elif defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER);
#else
    return "unknown";
#endif
}

std::string operating_system_description() {
#if defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#elif defined(_WIN32)
    return "Windows";
#else
    return "unknown";
#endif
}

std::string architecture_description() {
#if defined(__aarch64__) || defined(__arm64__)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#else
    return "unknown";
#endif
}

std::string build_type_description() {
#ifdef QUANT_ENGINE_BUILD_TYPE
    return QUANT_ENGINE_BUILD_TYPE;
#else
    return "unknown";
#endif
}

std::string current_timestamp_utc() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &value);
#else
    gmtime_r(&value, &utc);
#endif
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

std::string benchmark_result_csv_header() {
    return "name,timestamp_utc,operations,elapsed_seconds,ops_per_second,min_ns,mean_ns,"
           "p50_ns,p95_ns,p99_ns,max_ns,trades,fills,active_orders,bid_levels,"
           "ask_levels,checksum,seed,runs,invariants,build_type,compiler,os,arch,"
           "composition";
}

std::string benchmark_result_to_csv(const BenchmarkResult& result) {
    const auto value = [&](const auto member) {
        return result.latency.has_value() ? member(*result.latency) : 0.0;
    };
    std::ostringstream output;
    output << std::setprecision(12) << result.name << ',' << result.timestamp_utc
           << ',' << result.operations << ',' << result.elapsed_seconds << ','
           << result.operations_per_second << ','
           << value([](const LatencyStatistics& item) { return item.minimum_ns; })
           << ','
           << value([](const LatencyStatistics& item) { return item.mean_ns; })
           << ','
           << value([](const LatencyStatistics& item) { return item.p50_ns; })
           << ','
           << value([](const LatencyStatistics& item) { return item.p95_ns; })
           << ','
           << value([](const LatencyStatistics& item) { return item.p99_ns; })
           << ','
           << value([](const LatencyStatistics& item) { return item.maximum_ns; })
           << ',' << result.trades << ',' << result.fills << ','
           << result.active_orders << ',' << result.bid_levels << ','
           << result.ask_levels << ',' << result.checksum << ',' << result.seed
           << ',' << result.runs << ',' << (result.invariants_valid ? 1 : 0)
           << ',' << result.build_type << ',' << result.compiler << ','
           << result.operating_system << ',' << result.architecture << ','
           << result.composition;
    return output.str();
}

void write_csv_report(const std::filesystem::path& output,
                      const std::vector<BenchmarkResult>& results) {
    std::ofstream stream(output);
    if (!stream) {
        throw std::runtime_error("cannot open benchmark CSV output");
    }
    stream << benchmark_result_csv_header() << '\n';
    for (const auto& result : results) {
        stream << benchmark_result_to_csv(result) << '\n';
    }
}

void print_report(std::ostream& output, const BenchmarkResult& result) {
    output << "QUANT ENGINE BENCHMARK\n"
           << "Benchmark: " << result.name << '\n'
           << "Timestamp UTC: " << result.timestamp_utc << '\n'
           << "Operations: " << result.operations << '\n'
           << "Seed: " << result.seed << '\n'
           << "Runs: " << result.runs << '\n'
           << "Build: " << result.build_type << '\n'
           << "Compiler: " << result.compiler << '\n'
           << "Environment: " << result.operating_system << ' '
           << result.architecture << '\n'
           << "Workload: " << result.composition << '\n'
           << std::fixed << std::setprecision(3)
           << "Elapsed seconds: " << result.elapsed_seconds << '\n'
           << "Throughput ops/sec: " << result.operations_per_second << '\n';
    if (result.latency.has_value()) {
        output << "Latency ns min/mean/p50/p95/p99/max: "
               << result.latency->minimum_ns << " / "
               << result.latency->mean_ns << " / " << result.latency->p50_ns
               << " / " << result.latency->p95_ns << " / "
               << result.latency->p99_ns << " / "
               << result.latency->maximum_ns << '\n';
    }
    output << "Trades/Fills: " << result.trades << '/' << result.fills << '\n'
           << "Active orders: " << result.active_orders << '\n'
           << "Bid/Ask levels: " << result.bid_levels << '/'
           << result.ask_levels << '\n'
           << "Checksum: " << result.checksum << '\n'
           << "Invariants: " << (result.invariants_valid ? "PASS" : "FAIL")
           << "\n\n";
}

double throughput_change_percent(const double baseline,
                                 const double optimized) {
    if (baseline <= 0.0 || optimized < 0.0) {
        throw std::invalid_argument("throughput values must be positive");
    }
    return (optimized / baseline - 1.0) * 100.0;
}

double latency_change_percent(const double baseline, const double optimized) {
    if (baseline <= 0.0 || optimized < 0.0) {
        throw std::invalid_argument("latency values must be positive");
    }
    return (baseline - optimized) / baseline * 100.0;
}

}  // namespace quant_engine::benchmarks
