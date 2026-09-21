#include "quant_engine/execution/execution_summary.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace quant_engine::execution {
namespace {

const char* status_name(const ParentOrderStatus status) {
    switch (status) {
        case ParentOrderStatus::Pending:
            return "PENDING";
        case ParentOrderStatus::Active:
            return "ACTIVE";
        case ParentOrderStatus::PartiallyFilled:
            return "PARTIALLY_FILLED";
        case ParentOrderStatus::Filled:
            return "FILLED";
        case ParentOrderStatus::Cancelled:
            return "CANCELLED";
        case ParentOrderStatus::Expired:
            return "EXPIRED";
    }
    return "UNKNOWN";
}

}  // namespace

ExecutionSummary build_execution_summary(
    const ParentOrder& parent, std::string algorithm,
    const std::size_t child_order_count,
    const std::vector<ExecutionFill>& fills,
    const Quantity observed_market_volume) {
    Quantity fill_quantity = 0;
    long double notional = 0.0L;
    std::optional<Timestamp> first_fill;
    std::optional<Timestamp> last_fill;
    for (const ExecutionFill& fill : fills) {
        fill_quantity += fill.quantity;
        notional += static_cast<long double>(fill.price) *
                    static_cast<long double>(fill.quantity);
        first_fill = first_fill.has_value()
                         ? std::min(*first_fill, fill.timestamp)
                         : std::optional<Timestamp>{fill.timestamp};
        last_fill = last_fill.has_value()
                        ? std::max(*last_fill, fill.timestamp)
                        : std::optional<Timestamp>{fill.timestamp};
    }
    if (fill_quantity != parent.executed_quantity()) {
        throw std::logic_error("parent executed quantity differs from child trade fills");
    }
    const std::optional<double> average_price =
        fill_quantity == 0
            ? std::nullopt
            : std::optional<double>{static_cast<double>(
                  notional / static_cast<long double>(fill_quantity))};
    const std::optional<double> realized_participation =
        observed_market_volume == 0
            ? std::nullopt
            : std::optional<double>{
                  static_cast<double>(fill_quantity) /
                  static_cast<double>(observed_market_volume)};
    return ExecutionSummary{
        .parent_order_id = parent.parent_order_id(),
        .algorithm = std::move(algorithm),
        .side = parent.side(),
        .requested_quantity = parent.total_quantity(),
        .executed_quantity = parent.executed_quantity(),
        .remaining_quantity = parent.remaining_quantity(),
        .number_of_child_orders = child_order_count,
        .number_of_fills = fills.size(),
        .average_execution_price = average_price,
        .first_fill_time = first_fill,
        .last_fill_time = last_fill,
        .realized_participation = realized_participation,
        .status = parent.status(),
    };
}

std::string ExecutionSummary::to_string() const {
    std::ostringstream output;
    output << "EXECUTION REPORT\n"
           << "------------------------------------\n"
           << "Parent Order:        " << parent_order_id << '\n'
           << "Algorithm:           " << algorithm << '\n'
           << "Side:                " << (side == Side::Buy ? "BUY" : "SELL") << '\n'
           << "Requested Qty:       " << requested_quantity << '\n'
           << "Executed Qty:        " << executed_quantity << '\n'
           << "Remaining Qty:       " << remaining_quantity << '\n'
           << "Child Orders:        " << number_of_child_orders << '\n'
           << "Fills:               " << number_of_fills << '\n'
           << "Average Price:       ";
    if (average_execution_price.has_value()) {
        output << std::fixed << std::setprecision(4) << *average_execution_price;
    } else {
        output << "N/A";
    }
    output << '\n' << "Status:              " << status_name(status) << '\n';
    return output.str();
}

}  // namespace quant_engine::execution
