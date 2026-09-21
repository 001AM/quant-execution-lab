#include "benchmark_report.hpp"
#include "benchmark_runner.hpp"
#include "benchmark_scenarios.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace quant_engine::benchmarks;

struct Options {
    std::string benchmark{"mixed"};
    std::size_t operations{100'000};
    std::uint64_t seed{42};
    std::size_t runs{5};
    std::size_t price_levels{100};
    bool latency{true};
    bool matrix{false};
    bool reserve_capacity{false};
    std::optional<std::filesystem::path> output;
    std::optional<double> baseline_throughput;
    double max_regression_percent{25.0};
};

std::size_t parse_size(const char* value, const std::string_view option) {
    const auto parsed = std::stoull(value);
    if (parsed == 0) {
        throw std::invalid_argument(std::string{option} + " must be positive");
    }
    return static_cast<std::size_t>(parsed);
}

Options parse_options(const int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        const auto next = [&]() -> const char* {
            if (index + 1 >= argc) {
                throw std::invalid_argument(std::string{argument} +
                                            " requires a value");
            }
            return argv[++index];
        };
        if (argument == "--benchmark") {
            options.benchmark = next();
        } else if (argument == "--operations") {
            options.operations = parse_size(next(), argument);
        } else if (argument == "--seed") {
            options.seed = static_cast<std::uint64_t>(
                std::stoull(next()));
        } else if (argument == "--runs") {
            options.runs = parse_size(next(), argument);
        } else if (argument == "--price-levels") {
            options.price_levels = parse_size(next(), argument);
        } else if (argument == "--output") {
            options.output = next();
        } else if (argument == "--throughput-only") {
            options.latency = false;
        } else if (argument == "--matrix") {
            options.matrix = true;
        } else if (argument == "--reserve") {
            options.reserve_capacity = true;
        } else if (argument == "--baseline-throughput") {
            options.baseline_throughput = std::stod(next());
        } else if (argument == "--max-regression-percent") {
            options.max_regression_percent = std::stod(next());
        } else if (argument == "--help") {
            std::cout
                << "Usage: quant_engine_benchmarks [options]\n"
                << "  --benchmark submit|cancel|cancel-level|modify|matching|"
                   "matching-walk|market|market-exhaust|best|mixed|all\n"
                << "  --operations N --seed N --runs N --price-levels N\n"
                << "  --matrix --throughput-only --reserve --output FILE\n"
                << "  --baseline-throughput OPS --max-regression-percent PCT\n";
            std::exit(0);
        } else {
            throw std::invalid_argument("unknown option: " +
                                        std::string{argument});
        }
    }
    return options;
}

std::vector<BenchmarkScenario> scenarios(const std::string& name,
                                         const std::size_t operations,
                                         const std::size_t price_levels,
                                         const std::uint64_t seed) {
    if (name == "submit") {
        return {make_submit_scenario(operations, price_levels, seed)};
    }
    if (name == "cancel") {
        return {make_cancel_scenario(operations, false, seed)};
    }
    if (name == "cancel-level") {
        return {make_cancel_scenario(operations, true, seed)};
    }
    if (name == "modify") {
        return {make_modify_scenario(operations, "decrease", seed),
                make_modify_scenario(operations, "increase", seed),
                make_modify_scenario(operations, "price", seed)};
    }
    if (name == "matching") {
        return {make_matching_scenario(operations, price_levels, false, seed)};
    }
    if (name == "matching-walk") {
        return {make_matching_walk_scenario(operations, 4, seed)};
    }
    if (name == "market") {
        return {make_matching_scenario(operations, price_levels, true, seed)};
    }
    if (name == "market-exhaust") {
        return {make_market_exhaust_scenario(operations, seed)};
    }
    if (name == "best") {
        return {make_best_price_scenario(operations, price_levels, seed)};
    }
    if (name == "mixed") {
        return {make_mixed_scenario(operations, seed)};
    }
    if (name == "all") {
        return {make_submit_scenario(operations, price_levels, seed),
                make_cancel_scenario(operations, false, seed),
                make_cancel_scenario(operations, true, seed),
                make_modify_scenario(operations, "decrease", seed),
                make_modify_scenario(operations, "increase", seed),
                make_modify_scenario(operations, "price", seed),
                make_matching_scenario(operations, 1, false, seed),
                make_matching_scenario(operations, price_levels, false, seed),
                make_matching_scenario(operations, price_levels, true, seed),
                make_mixed_scenario(operations, seed)};
    }
    throw std::invalid_argument("unknown benchmark: " + name);
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        if (build_type_description() != "Release") {
            std::cerr << "warning: performance results should use a Release build\n";
        }
        std::vector<BenchmarkResult> results;
        const std::vector<std::size_t> sizes =
            options.matrix ? standard_scaling_sizes()
                           : std::vector<std::size_t>{options.operations};
        for (const std::size_t size : sizes) {
            for (const auto& scenario : scenarios(
                     options.benchmark, size, options.price_levels, options.seed)) {
                BenchmarkResult result = run_benchmark(
                    scenario,
                    BenchmarkRunConfig{options.seed, options.runs,
                                       options.latency,
                                       options.reserve_capacity});
                print_report(std::cout, result);
                results.push_back(std::move(result));
            }
        }
        if (options.output.has_value()) {
            write_csv_report(*options.output, results);
        }
        if (options.baseline_throughput.has_value()) {
            if (results.size() != 1) {
                throw std::invalid_argument(
                    "regression check requires exactly one benchmark result");
            }
            const double change = throughput_change_percent(
                *options.baseline_throughput,
                results.front().operations_per_second);
            std::cout << "Throughput change vs supplied baseline: " << change
                      << "%\n";
            if (change < -options.max_regression_percent) {
                std::cerr << "performance regression threshold exceeded\n";
                return 2;
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "benchmark error: " << error.what() << '\n';
        return 1;
    }
}
