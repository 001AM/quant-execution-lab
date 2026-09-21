#include "quant_engine/execution/execution_scheduler.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace quant_engine::execution {

ExecutionScheduler::ExecutionScheduler(MatchingEngine& engine,
                                       const SchedulerConfig config)
    : engine_{engine}, config_{config} {}

void ExecutionScheduler::add_parent(ParentOrder parent,
                                    std::vector<ChildOrder> schedule,
                                    std::string algorithm) {
    const ParentOrderId parent_id = parent.parent_order_id();
    if (parents_.contains(parent_id)) {
        throw std::invalid_argument("duplicate parent order id");
    }
    if (parent.status() != ParentOrderStatus::Pending) {
        throw std::invalid_argument("new scheduler parent must be pending");
    }

    Quantity scheduled_quantity = 0;
    for (const ChildOrder& child : schedule) {
        if (child.order_id == 0 || child.parent_order_id != parent_id ||
            child.side != parent.side() || child.quantity == 0 ||
            child.scheduled_time < parent.start_time() ||
            child.scheduled_time >= parent.end_time() || child.schedule_sequence == 0) {
            throw std::invalid_argument("invalid child schedule");
        }
        if (child.type == OrderType::Market && child.price.has_value()) {
            throw std::invalid_argument("market child must not have a price");
        }
        if (child.type == OrderType::Limit &&
            (!child.price.has_value() || *child.price <= 0)) {
            throw std::invalid_argument("limit child requires a positive price");
        }
        if (child_order_ids_.contains(child.order_id)) {
            throw std::invalid_argument("duplicate child order id");
        }
        if (child.quantity >
            std::numeric_limits<Quantity>::max() - scheduled_quantity) {
            throw std::overflow_error("scheduled child quantity overflow");
        }
        scheduled_quantity += child.quantity;
    }
    if (!schedule.empty() && scheduled_quantity != parent.remaining_quantity()) {
        throw std::invalid_argument("static child schedule must equal parent quantity");
    }

    const std::size_t pending_count = schedule.size();
    parents_.emplace(
        parent_id,
        ParentState{std::move(parent), std::move(algorithm), pending_count, 0, {}, {}});
    for (ChildOrder& child : schedule) {
        child_order_ids_.insert(child.order_id);
        schedule_.push_back(ScheduledEntry{std::move(child), false});
    }
    std::sort(schedule_.begin(), schedule_.end(),
              [](const ScheduledEntry& left, const ScheduledEntry& right) {
                  if (left.child.scheduled_time != right.child.scheduled_time) {
                      return left.child.scheduled_time < right.child.scheduled_time;
                  }
                  if (left.child.schedule_sequence != right.child.schedule_sequence) {
                      return left.child.schedule_sequence < right.child.schedule_sequence;
                  }
                  return left.child.order_id < right.child.order_id;
              });
}

ChildExecution ExecutionScheduler::submit_child(ParentState& state,
                                                 ChildOrder child) {
    const OrderRequest request{
        .id = child.order_id,
        .side = child.side,
        .type = child.type,
        .quantity = child.quantity,
        .price = child.price,
        .timestamp = child.scheduled_time,
    };
    SubmitResult result = engine_.submit(request);
    if (!result.accepted) {
        throw std::runtime_error("scheduled child order was rejected by matching engine");
    }
    if (result.report.executed_quantity > 0) {
        state.parent.record_fill(result.report.executed_quantity);
    }
    for (const Trade& trade : result.trades) {
        state.fills.push_back(
            ExecutionFill{trade.price, trade.quantity, child.scheduled_time});
        trade_to_child_.emplace(trade.trade_id, child);
    }
    state.executions.push_back(ChildExecution{child, result});
    return state.executions.back();
}

std::vector<ChildExecution> ExecutionScheduler::advance_to(
    const Timestamp timestamp) {
    if (current_time_.has_value() && timestamp < *current_time_) {
        throw std::invalid_argument("simulation time cannot move backward");
    }
    std::vector<ChildExecution> submitted;
    for (ScheduledEntry& entry : schedule_) {
        if (entry.submitted || entry.child.scheduled_time > timestamp) {
            continue;
        }
        entry.submitted = true;
        ParentState& state = parents_.at(entry.child.parent_order_id);
        if (state.pending_scheduled_children == 0) {
            throw std::logic_error("scheduled-child count underflow");
        }
        --state.pending_scheduled_children;
        if (!state.parent.can_generate_children()) {
            continue;
        }
        if (state.parent.status() == ParentOrderStatus::Pending) {
            state.parent.activate();
        }
        ChildOrder child = entry.child;
        const bool final_opportunity = state.pending_scheduled_children == 0;
        if (final_opportunity && config_.catch_up_final_slice) {
            child.quantity = state.parent.remaining_quantity();
        } else {
            child.quantity = std::min(child.quantity, state.parent.remaining_quantity());
        }
        if (child.quantity > 0) {
            submitted.push_back(submit_child(state, std::move(child)));
        }
    }
    current_time_ = timestamp;
    expire_at(timestamp);
    return submitted;
}

std::optional<ChildExecution> ExecutionScheduler::on_market_volume(
    const ParentOrderId parent_order_id, const POV& algorithm,
    const MarketVolumeObservation& observation, const OrderId child_order_id,
    const SequenceNumber schedule_sequence) {
    if (current_time_.has_value() && observation.timestamp < *current_time_) {
        throw std::invalid_argument("simulation time cannot move backward");
    }
    ParentState& state = parents_.at(parent_order_id);
    current_time_ = observation.timestamp;
    if (!state.parent.can_generate_children()) {
        return std::nullopt;
    }
    if (observation.timestamp < state.parent.start_time()) {
        throw std::invalid_argument("volume observation precedes parent start");
    }
    if (observation.timestamp >= state.parent.end_time()) {
        state.parent.expire();
        return std::nullopt;
    }
    if (observation.volume >
        std::numeric_limits<Quantity>::max() - state.observed_market_volume) {
        throw std::overflow_error("observed market volume overflow");
    }
    state.observed_market_volume += observation.volume;
    if (state.parent.status() == ParentOrderStatus::Pending) {
        state.parent.activate();
    }
    auto child = algorithm.generate_child(state.parent, observation, child_order_id,
                                          schedule_sequence);
    if (!child.has_value()) {
        return std::nullopt;
    }
    if (child_order_ids_.contains(child_order_id)) {
        throw std::invalid_argument("duplicate child order id");
    }
    child_order_ids_.insert(child_order_id);
    return submit_child(state, std::move(*child));
}

bool ExecutionScheduler::cancel_parent(const ParentOrderId parent_order_id) {
    const auto found = parents_.find(parent_order_id);
    if (found == parents_.end() || found->second.parent.is_terminal()) {
        return false;
    }
    found->second.parent.cancel();
    return true;
}

const ParentOrder* ExecutionScheduler::parent(
    const ParentOrderId parent_order_id) const noexcept {
    const auto found = parents_.find(parent_order_id);
    return found == parents_.end() ? nullptr : &found->second.parent;
}

const std::vector<ChildExecution>& ExecutionScheduler::child_executions(
    const ParentOrderId parent_order_id) const {
    return parents_.at(parent_order_id).executions;
}

std::optional<ChildOrder> ExecutionScheduler::child_for_trade(
    const TradeId trade_id) const {
    const auto found = trade_to_child_.find(trade_id);
    return found == trade_to_child_.end()
               ? std::nullopt
               : std::optional<ChildOrder>{found->second};
}

ExecutionSummary ExecutionScheduler::summary(
    const ParentOrderId parent_order_id) const {
    const ParentState& state = parents_.at(parent_order_id);
    return build_execution_summary(
        state.parent, state.algorithm, state.executions.size(), state.fills,
        state.observed_market_volume);
}

void ExecutionScheduler::expire_at(const Timestamp timestamp) {
    for (auto& [parent_id, state] : parents_) {
        static_cast<void>(parent_id);
        if (!state.parent.is_terminal() && timestamp >= state.parent.end_time()) {
            state.parent.expire();
        }
    }
}

}  // namespace quant_engine::execution
