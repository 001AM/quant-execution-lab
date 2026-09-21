#include "quant_engine/matching_engine.hpp"

#include <utility>

namespace quant_engine {
namespace {

bool valid_side(const Side side) noexcept {
    return side == Side::Buy || side == Side::Sell;
}

bool valid_type(const OrderType type) noexcept {
    return type == OrderType::Limit || type == OrderType::Market;
}

}  // namespace

void MatchingEngine::reserve_capacity(
    const std::size_t expected_active_orders,
    const std::size_t expected_completed_orders,
    const std::size_t expected_trades,
    const std::size_t expected_orders_per_level) {
    book_.reserve_capacity(expected_active_orders, expected_completed_orders,
                           expected_trades, expected_orders_per_level);
}

ExecutionReport MatchingEngine::report_for(const Order& order) {
    return ExecutionReport{
        .order_id = order.order_id(),
        .status = order.status(),
        .original_quantity = order.original_quantity(),
        .executed_quantity = order.filled_quantity(),
        .remaining_quantity = order.remaining_quantity(),
        .average_execution_price = order.average_execution_price(),
        .last_execution_price = order.last_execution_price(),
        .last_execution_quantity = order.last_execution_quantity(),
        .sequence = order.sequence(),
    };
}

ExecutionReport MatchingEngine::rejection_report(const OrderRequest& request) {
    return ExecutionReport{
        .order_id = request.id,
        .status = OrderStatus::Rejected,
        .original_quantity = request.quantity,
        .executed_quantity = 0,
        .remaining_quantity = request.quantity,
        .average_execution_price = std::nullopt,
        .last_execution_price = std::nullopt,
        .last_execution_quantity = std::nullopt,
        .sequence = 0,
    };
}

ExecutionReport MatchingEngine::report_for(const OrderView& order) {
    return ExecutionReport{
        .order_id = order.order_id,
        .status = order.status,
        .original_quantity = order.original_quantity,
        .executed_quantity = order.executed_quantity,
        .remaining_quantity = order.remaining_quantity,
        .average_execution_price = std::nullopt,
        .last_execution_price = std::nullopt,
        .last_execution_quantity = std::nullopt,
        .sequence = order.sequence,
    };
}

SubmitResult MatchingEngine::submit(const OrderRequest& request) {
    const auto reject = [&](const RejectReason reason) {
        const ExecutionReport report = rejection_report(request);
        if (request.id != 0 && !book_.has_used_order_id(request.id) &&
            !rejected_orders_.contains(request.id)) {
            rejected_orders_.emplace(
                request.id,
                OrderView{
                    .order_id = request.id,
                    .side = request.side,
                    .type = request.type,
                    .price = request.price,
                    .original_quantity = request.quantity,
                    .executed_quantity = 0,
                    .remaining_quantity = request.quantity,
                    .sequence = 0,
                    .status = OrderStatus::Rejected,
                });
        }
        return SubmitResult{false, reason, {}, report};
    };
    if (request.id == 0) {
        return reject(RejectReason::InvalidOrderId);
    }
    if (!valid_side(request.side)) {
        return reject(RejectReason::InvalidSide);
    }
    if (!valid_type(request.type)) {
        return reject(RejectReason::InvalidOrderType);
    }
    if (request.quantity == 0) {
        return reject(RejectReason::InvalidQuantity);
    }
    if (book_.has_used_order_id(request.id) || rejected_orders_.contains(request.id)) {
        return reject(RejectReason::DuplicateOrderId);
    }
    if (request.type == OrderType::Limit) {
        if (!request.price.has_value()) {
            return reject(RejectReason::MissingLimitPrice);
        }
        if (*request.price <= 0) {
            return reject(RejectReason::InvalidPrice);
        }
    } else if (request.price.has_value()) {
        return reject(RejectReason::UnexpectedMarketPrice);
    }

    std::vector<Trade> generated =
        request.type == OrderType::Limit
            ? book_.add_limit_order(request.id, request.side, *request.price,
                                    request.quantity, request.timestamp)
            : book_.add_market_order(request.id, request.side, request.quantity,
                                     request.timestamp);
    return SubmitResult{
        true,
        RejectReason::None,
        std::move(generated),
        report_for(*book_.find_order(request.id)),
    };
}

CancelResult MatchingEngine::cancel(const OrderId order_id) {
    const Order* order = book_.find_order(order_id);
    if (order == nullptr) {
        const auto rejected = rejected_orders_.find(order_id);
        if (rejected != rejected_orders_.end()) {
            return CancelResult{
                false, RejectReason::OrderNotActive, report_for(rejected->second)};
        }
        return CancelResult{false, RejectReason::OrderNotFound, std::nullopt};
    }
    if (!order->is_active()) {
        return CancelResult{false, RejectReason::OrderNotActive, report_for(*order)};
    }
    static_cast<void>(book_.cancel_order(order_id));
    return CancelResult{
        true,
        RejectReason::None,
        report_for(*book_.find_order(order_id)),
    };
}

ModifyResult MatchingEngine::modify(const OrderId order_id,
                                    const ModifyRequest& request) {
    const Order* current = book_.find_order(order_id);
    if (current == nullptr) {
        const auto rejected = rejected_orders_.find(order_id);
        if (rejected != rejected_orders_.end()) {
            return ModifyResult{
                false,
                false,
                RejectReason::OrderNotActive,
                {},
                report_for(rejected->second),
            };
        }
        return ModifyResult{false, false, RejectReason::OrderNotFound, {}, std::nullopt};
    }
    if (!current->is_active()) {
        return ModifyResult{
            false, false, RejectReason::OrderNotActive, {}, report_for(*current)};
    }
    if (current->type() != OrderType::Limit) {
        return ModifyResult{
            false, false, RejectReason::OrderNotActive, {}, report_for(*current)};
    }
    if (!request.new_price.has_value() && !request.new_quantity.has_value()) {
        return ModifyResult{
            false, false, RejectReason::InvalidModification, {}, report_for(*current)};
    }

    const Price target_price = request.new_price.value_or(current->price());
    const Quantity target_quantity =
        request.new_quantity.value_or(current->original_quantity());
    if (target_price <= 0) {
        return ModifyResult{
            false, false, RejectReason::InvalidPrice, {}, report_for(*current)};
    }
    if (target_quantity < current->filled_quantity()) {
        return ModifyResult{
            false, false, RejectReason::QuantityBelowExecuted, {}, report_for(*current)};
    }
    if (target_quantity == 0) {
        return ModifyResult{
            false, false, RejectReason::InvalidQuantity, {}, report_for(*current)};
    }
    if (target_price == current->price() &&
        target_quantity == current->original_quantity()) {
        return ModifyResult{
            false, false, RejectReason::InvalidModification, {}, report_for(*current)};
    }

    ReplaceOutcome outcome =
        book_.replace_order(order_id, target_price, target_quantity);
    return ModifyResult{
        true,
        outcome.priority_preserved,
        RejectReason::None,
        std::move(outcome.trades),
        report_for(*book_.find_order(order_id)),
    };
}

std::optional<OrderView> MatchingEngine::get_order(const OrderId order_id) const {
    if (auto order = book_.get_order(order_id); order.has_value()) {
        return order;
    }
    const auto rejected = rejected_orders_.find(order_id);
    return rejected == rejected_orders_.end()
               ? std::nullopt
               : std::optional<OrderView>{rejected->second};
}

}  // namespace quant_engine
