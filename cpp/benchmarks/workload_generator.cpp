#include "workload_generator.hpp"

#include <algorithm>
#include <deque>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace quant_engine::benchmarks {
namespace {

BenchmarkCommand limit_submit(const OrderId id, const Side side,
                              const Quantity quantity, const Price price) {
    return BenchmarkCommand{BenchmarkOperationType::SubmitLimit, id, side,
                            quantity, price, std::nullopt, std::nullopt};
}

BenchmarkCommand market_submit(const OrderId id, const Side side,
                               const Quantity quantity) {
    return BenchmarkCommand{BenchmarkOperationType::SubmitMarket, id, side,
                            quantity, std::nullopt, std::nullopt, std::nullopt};
}

}  // namespace

WorkloadGenerator::WorkloadGenerator(const std::uint64_t seed)
    : seed_(seed), random_(seed) {}

BenchmarkScenario WorkloadGenerator::submit(const std::size_t operations,
                                            const std::size_t price_levels) {
    if (operations == 0 || price_levels == 0) {
        throw std::invalid_argument(
            "submit workload requires operations and price levels");
    }
    BenchmarkScenario scenario{"submit", {}, {}, "100% resting LIMIT submits"};
    scenario.expected_active_orders = operations;
    scenario.expected_orders_per_level =
        (operations + price_levels - 1) / price_levels;
    scenario.commands.reserve(operations);
    for (std::size_t index = 0; index < operations; ++index) {
        const bool buy = index % 2 == 0;
        const auto offset = static_cast<Price>(index % price_levels);
        const Price price = buy ? 1'000'000 - offset : 1'001'000 + offset;
        scenario.commands.push_back(limit_submit(
            static_cast<OrderId>(index + 1), buy ? Side::Buy : Side::Sell, 1,
            price));
    }
    return scenario;
}

BenchmarkScenario WorkloadGenerator::cancel(const std::size_t operations,
                                            const bool remove_levels) {
    if (operations == 0) {
        throw std::invalid_argument("cancel workload requires operations");
    }
    BenchmarkScenario scenario{
        remove_levels ? "cancel-final-level" : "cancel-existing-level", {}, {},
        remove_levels ? "100% valid cancels; each removes a price level"
                      : "100% valid cancels; shared level remains until final cancel"};
    scenario.expected_active_orders = operations;
    scenario.expected_completed_orders = operations;
    scenario.expected_orders_per_level = remove_levels ? 1 : operations;
    scenario.setup.reserve(operations);
    scenario.commands.reserve(operations);
    for (std::size_t index = 0; index < operations; ++index) {
        const OrderId id = static_cast<OrderId>(index + 1);
        const Price price = remove_levels
                                ? 2'000'000 - static_cast<Price>(index)
                                : 1'000'000;
        scenario.setup.push_back(limit_submit(id, Side::Buy, 1, price));
        scenario.commands.push_back(BenchmarkCommand{
            BenchmarkOperationType::Cancel, id, Side::Buy, 0, std::nullopt,
            std::nullopt, std::nullopt});
    }
    std::shuffle(scenario.commands.begin(), scenario.commands.end(), random_);
    return scenario;
}

BenchmarkScenario WorkloadGenerator::modify(const std::size_t operations,
                                            const std::string_view mode) {
    if (operations == 0) {
        throw std::invalid_argument("modify workload requires operations");
    }
    if (mode != "decrease" && mode != "increase" && mode != "price") {
        throw std::invalid_argument("modify mode must be decrease, increase, or price");
    }
    BenchmarkScenario scenario{"modify-" + std::string{mode}, {}, {},
                               "100% valid " + std::string{mode} +
                                   " modifications"};
    scenario.expected_active_orders = operations;
    scenario.expected_orders_per_level = (operations + 99) / 100;
    scenario.setup.reserve(operations);
    scenario.commands.reserve(operations);
    for (std::size_t index = 0; index < operations; ++index) {
        const OrderId id = static_cast<OrderId>(index + 1);
        const Price price = 1'000'000 - static_cast<Price>(index % 100);
        const Quantity quantity = mode == "decrease" ? 2 : 1;
        scenario.setup.push_back(limit_submit(id, Side::Buy, quantity, price));
        BenchmarkCommand command{BenchmarkOperationType::Modify,
                                 id,
                                 Side::Buy,
                                 0,
                                 std::nullopt,
                                 std::nullopt,
                                 std::nullopt};
        if (mode == "decrease") {
            command.new_quantity = 1;
        } else if (mode == "increase") {
            command.new_quantity = 2;
        } else {
            command.new_price = 900'000 - static_cast<Price>(index % 100);
        }
        scenario.commands.push_back(command);
    }
    return scenario;
}

BenchmarkScenario WorkloadGenerator::matching(const std::size_t operations,
                                              const std::size_t price_levels,
                                              const bool market_orders) {
    if (operations == 0 || price_levels == 0) {
        throw std::invalid_argument(
            "matching workload requires operations and price levels");
    }
    BenchmarkScenario scenario{
        market_orders ? "market-matching"
                      : (price_levels == 1 ? "matching-single-level"
                                           : "matching-multi-level"), {}, {},
        market_orders ? "MARKET BUY; one fill per operation"
                      : "crossing LIMIT BUY; one fill per operation"};
    scenario.expected_active_orders = operations;
    scenario.expected_completed_orders = operations * 2;
    scenario.expected_trades = operations;
    scenario.expected_orders_per_level =
        (operations + price_levels - 1) / price_levels;
    scenario.setup.reserve(operations);
    scenario.commands.reserve(operations);
    for (std::size_t index = 0; index < operations; ++index) {
        const Price price =
            1'000'000 + static_cast<Price>(index % price_levels);
        scenario.setup.push_back(limit_submit(
            static_cast<OrderId>(index + 1), Side::Sell, 1, price));
        const OrderId incoming_id = static_cast<OrderId>(operations + index + 1);
        scenario.commands.push_back(
            market_orders
                ? market_submit(incoming_id, Side::Buy, 1)
                : limit_submit(incoming_id, Side::Buy, 1, 2'000'000));
    }
    return scenario;
}

BenchmarkScenario WorkloadGenerator::mixed(const std::size_t operations) {
    if (operations == 0) {
        throw std::invalid_argument("mixed workload requires operations");
    }
    BenchmarkScenario scenario{
        "mixed", {}, {},
        "target 60% resting limit, 15% cancel, 10% modify, "
        "10% crossing limit, 5% market"};
    scenario.expected_active_orders = operations;
    scenario.expected_completed_orders = operations;
    scenario.expected_trades = operations / 5;
    scenario.expected_orders_per_level = operations;
    scenario.commands.reserve(operations);
    std::deque<std::pair<OrderId, Quantity>> active_fifo;
    OrderId next_id = 1;
    std::size_t limits = 0;
    std::size_t cancels = 0;
    std::size_t modifies = 0;
    std::size_t crossings = 0;
    std::size_t markets = 0;
    std::uniform_int_distribution<int> distribution(0, 99);

    for (std::size_t index = 0; index < operations; ++index) {
        const int selection = distribution(random_);
        if (active_fifo.empty() || selection < 60) {
            const OrderId id = next_id++;
            scenario.commands.push_back(
                limit_submit(id, Side::Buy, 1, 1'000'000));
            active_fifo.emplace_back(id, 1);
            ++limits;
        } else if (selection < 75) {
            const auto [id, quantity] = active_fifo.front();
            static_cast<void>(quantity);
            active_fifo.pop_front();
            scenario.commands.push_back(BenchmarkCommand{
                BenchmarkOperationType::Cancel, id, Side::Buy, 0, std::nullopt,
                std::nullopt, std::nullopt});
            ++cancels;
        } else if (selection < 85) {
            auto [id, quantity] = active_fifo.front();
            active_fifo.pop_front();
            const Quantity new_quantity = quantity == 1 ? 2 : 1;
            scenario.commands.push_back(BenchmarkCommand{
                BenchmarkOperationType::Modify, id, Side::Buy, 0, std::nullopt,
                std::nullopt, new_quantity});
            if (new_quantity > quantity) {
                active_fifo.emplace_back(id, new_quantity);
            } else {
                active_fifo.emplace_front(id, new_quantity);
            }
            ++modifies;
        } else if (selection < 95) {
            const auto [id, quantity] = active_fifo.front();
            static_cast<void>(id);
            active_fifo.pop_front();
            scenario.commands.push_back(
                limit_submit(next_id++, Side::Sell, quantity, 1'000'000));
            ++crossings;
        } else {
            const auto [id, quantity] = active_fifo.front();
            static_cast<void>(id);
            active_fifo.pop_front();
            scenario.commands.push_back(
                market_submit(next_id++, Side::Sell, quantity));
            ++markets;
        }
    }

    std::ostringstream composition;
    composition << "limit=" << limits << ";cancel=" << cancels
                << ";modify=" << modifies << ";cross=" << crossings
                << ";market=" << markets;
    scenario.composition = composition.str();
    static_cast<void>(seed_);
    return scenario;
}

}  // namespace quant_engine::benchmarks
