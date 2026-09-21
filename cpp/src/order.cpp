#include "quant_engine/order.hpp"

#include <stdexcept>

namespace quant_engine {
namespace {

void validate_side(const Side side) {
    if (side != Side::Buy && side != Side::Sell) {
        throw std::invalid_argument("order side is invalid");
    }
}

void validate_type(const OrderType type) {
    if (type != OrderType::Limit && type != OrderType::Market) {
        throw std::invalid_argument("order type is invalid");
    }
}

}  // namespace

Order::Order(const OrderId order_id, const Side side, const OrderType type,
             const Price price, const Quantity quantity,
             const SequenceNumber sequence, const Timestamp timestamp)
    : order_id_{order_id},
      side_{side},
      type_{type},
      price_{price},
      original_quantity_{quantity},
      remaining_quantity_{quantity},
      sequence_{sequence},
      timestamp_{timestamp} {
    if (order_id == 0) {
        throw std::invalid_argument("order id must be positive");
    }
    validate_side(side);
    validate_type(type);
    if (quantity == 0) {
        throw std::invalid_argument("order quantity must be positive");
    }
    if (sequence == 0) {
        throw std::invalid_argument("order sequence must be positive");
    }
    if (type == OrderType::Limit && price <= 0) {
        throw std::invalid_argument("limit order price must be positive");
    }
}

bool Order::is_active() const noexcept {
    return status_ == OrderStatus::Active || status_ == OrderStatus::PartiallyFilled;
}

std::optional<double> Order::average_execution_price() const noexcept {
    const Quantity executed = filled_quantity();
    if (executed == 0) {
        return std::nullopt;
    }
    return static_cast<double>(executed_notional_ / static_cast<long double>(executed));
}

void Order::activate() {
    if (status_ != OrderStatus::New) {
        throw std::logic_error("only new orders can become active");
    }
    status_ = OrderStatus::Active;
}

void Order::fill(const Quantity quantity) {
    if (type_ == OrderType::Market) {
        throw std::logic_error("market-order fills require an execution price");
    }
    fill(quantity, price_);
}

void Order::fill(const Quantity quantity, const Price execution_price) {
    if (!is_active()) {
        throw std::logic_error("only active orders can be filled");
    }
    if (quantity == 0) {
        throw std::invalid_argument("fill quantity must be positive");
    }
    if (quantity > remaining_quantity_) {
        throw std::invalid_argument("fill quantity exceeds remaining quantity");
    }
    if (execution_price <= 0) {
        throw std::invalid_argument("execution price must be positive");
    }
    executed_notional_ += static_cast<long double>(execution_price) *
                          static_cast<long double>(quantity);
    last_execution_price_ = execution_price;
    last_execution_quantity_ = quantity;
    remaining_quantity_ -= quantity;
    status_ = remaining_quantity_ == 0 ? OrderStatus::Filled
                                       : OrderStatus::PartiallyFilled;
}

void Order::replace(const Price price, const Quantity total_quantity,
                    const SequenceNumber sequence) {
    if (!is_active()) {
        throw std::logic_error("only active orders can be replaced");
    }
    if (type_ != OrderType::Limit) {
        throw std::logic_error("market orders cannot be replaced");
    }
    if (price <= 0) {
        throw std::invalid_argument("replacement price must be positive");
    }
    if (sequence == 0) {
        throw std::invalid_argument("replacement sequence must be positive");
    }
    const Quantity executed = filled_quantity();
    if (total_quantity < executed) {
        throw std::invalid_argument("replacement quantity is below executed quantity");
    }
    price_ = price;
    original_quantity_ = total_quantity;
    remaining_quantity_ = total_quantity - executed;
    sequence_ = sequence;
    if (remaining_quantity_ == 0) {
        status_ = OrderStatus::Cancelled;
    } else {
        status_ = executed == 0 ? OrderStatus::Active : OrderStatus::PartiallyFilled;
    }
}

void Order::cancel() {
    if (!is_active()) {
        throw std::logic_error("only active orders can be cancelled");
    }
    status_ = OrderStatus::Cancelled;
}

}  // namespace quant_engine
