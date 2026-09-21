#pragma once

#include "quant_engine/types.hpp"

#include <optional>

namespace quant_engine {

struct OrderView {
    OrderId order_id;
    Side side;
    OrderType type;
    std::optional<Price> price;
    Quantity original_quantity;
    Quantity executed_quantity;
    Quantity remaining_quantity;
    SequenceNumber sequence;
    OrderStatus status;
};

struct ExecutionReport {
    OrderId order_id;
    OrderStatus status;
    Quantity original_quantity;
    Quantity executed_quantity;
    Quantity remaining_quantity;
    std::optional<double> average_execution_price;
    std::optional<Price> last_execution_price;
    std::optional<Quantity> last_execution_quantity;
    SequenceNumber sequence;
};

}  // namespace quant_engine
