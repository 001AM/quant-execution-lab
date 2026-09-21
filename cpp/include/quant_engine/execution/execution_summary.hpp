#pragma once

#include "quant_engine/execution/parent_order.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace quant_engine::execution {

struct ExecutionFill {
    Price price;
    Quantity quantity;
    Timestamp timestamp;
};

struct ExecutionSummary {
    ParentOrderId parent_order_id;
    std::string algorithm;
    Side side;
    Quantity requested_quantity;
    Quantity executed_quantity;
    Quantity remaining_quantity;
    std::size_t number_of_child_orders;
    std::size_t number_of_fills;
    std::optional<double> average_execution_price;
    std::optional<Timestamp> first_fill_time;
    std::optional<Timestamp> last_fill_time;
    std::optional<double> realized_participation;
    ParentOrderStatus status;

    [[nodiscard]] std::string to_string() const;
};

[[nodiscard]] ExecutionSummary build_execution_summary(
    const ParentOrder& parent, std::string algorithm,
    std::size_t child_order_count, const std::vector<ExecutionFill>& fills,
    Quantity observed_market_volume);

}  // namespace quant_engine::execution
