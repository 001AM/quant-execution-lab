#include "quant_engine/execution/parent_order.hpp"

#include <stdexcept>
#include <utility>

namespace quant_engine::execution {

ParentOrder::ParentOrder(const ParentOrderId parent_order_id, std::string symbol,
                         const Side side, const Quantity total_quantity,
                         const Timestamp start_time, const Timestamp end_time)
    : parent_order_id_{parent_order_id},
      symbol_{std::move(symbol)},
      side_{side},
      total_quantity_{total_quantity},
      start_time_{start_time},
      end_time_{end_time} {
    if (parent_order_id == 0) {
        throw std::invalid_argument("parent order id must be positive");
    }
    if (symbol_.empty()) {
        throw std::invalid_argument("parent symbol must not be empty");
    }
    if (side != Side::Buy && side != Side::Sell) {
        throw std::invalid_argument("parent side is invalid");
    }
    if (total_quantity == 0) {
        throw std::invalid_argument("parent quantity must be positive");
    }
    if (start_time >= end_time) {
        throw std::invalid_argument("parent start time must precede end time");
    }
}

bool ParentOrder::is_terminal() const noexcept {
    return status_ == ParentOrderStatus::Filled ||
           status_ == ParentOrderStatus::Cancelled ||
           status_ == ParentOrderStatus::Expired;
}

bool ParentOrder::can_generate_children() const noexcept {
    return !is_terminal() && remaining_quantity() > 0;
}

void ParentOrder::activate() {
    if (status_ != ParentOrderStatus::Pending) {
        throw std::logic_error("only pending parent orders can be activated");
    }
    status_ = ParentOrderStatus::Active;
}

void ParentOrder::record_fill(const Quantity quantity) {
    if (status_ != ParentOrderStatus::Active &&
        status_ != ParentOrderStatus::PartiallyFilled) {
        throw std::logic_error("parent order is not executable");
    }
    if (quantity == 0 || quantity > remaining_quantity()) {
        throw std::invalid_argument("invalid parent fill quantity");
    }
    executed_quantity_ += quantity;
    status_ = remaining_quantity() == 0 ? ParentOrderStatus::Filled
                                        : ParentOrderStatus::PartiallyFilled;
}

void ParentOrder::cancel() {
    if (is_terminal()) {
        throw std::logic_error("terminal parent order cannot be cancelled");
    }
    status_ = ParentOrderStatus::Cancelled;
}

void ParentOrder::expire() {
    if (is_terminal()) {
        throw std::logic_error("terminal parent order cannot expire");
    }
    status_ = ParentOrderStatus::Expired;
}

}  // namespace quant_engine::execution
