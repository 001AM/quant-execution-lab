#pragma once

#include "quant_engine/execution_report.hpp"
#include "quant_engine/trade.hpp"

#include <optional>
#include <vector>

namespace quant_engine {

struct OrderRequest {
    OrderId id;
    Side side;
    OrderType type;
    Quantity quantity;
    std::optional<Price> price;
    Timestamp timestamp{};
};

struct ModifyRequest {
    std::optional<Price> new_price;
    std::optional<Quantity> new_quantity;
};

struct SubmitResult {
    bool accepted;
    RejectReason reject_reason;
    std::vector<Trade> trades;
    ExecutionReport report;
};

struct CancelResult {
    bool cancelled;
    RejectReason reject_reason;
    std::optional<ExecutionReport> report;
};

struct ModifyResult {
    bool modified;
    bool priority_preserved;
    RejectReason reject_reason;
    std::vector<Trade> trades;
    std::optional<ExecutionReport> report;
};

}  // namespace quant_engine
