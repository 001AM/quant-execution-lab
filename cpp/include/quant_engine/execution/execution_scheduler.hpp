#pragma once

#include "quant_engine/execution/execution_summary.hpp"
#include "quant_engine/execution/pov.hpp"
#include "quant_engine/matching_engine.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace quant_engine::execution {

struct SchedulerConfig {
    bool catch_up_final_slice{true};
};

struct ChildExecution {
    ChildOrder child;
    SubmitResult result;
};

class ExecutionScheduler {
public:
    explicit ExecutionScheduler(MatchingEngine& engine,
                                SchedulerConfig config = {});

    void add_parent(ParentOrder parent, std::vector<ChildOrder> schedule,
                    std::string algorithm);
    [[nodiscard]] std::vector<ChildExecution> advance_to(Timestamp timestamp);
    [[nodiscard]] std::optional<ChildExecution> on_market_volume(
        ParentOrderId parent_order_id, const POV& algorithm,
        const MarketVolumeObservation& observation, OrderId child_order_id,
        SequenceNumber schedule_sequence);
    [[nodiscard]] bool cancel_parent(ParentOrderId parent_order_id);

    [[nodiscard]] const ParentOrder* parent(ParentOrderId parent_order_id) const noexcept;
    [[nodiscard]] const std::vector<ChildExecution>& child_executions(
        ParentOrderId parent_order_id) const;
    [[nodiscard]] std::optional<ChildOrder> child_for_trade(TradeId trade_id) const;
    [[nodiscard]] ExecutionSummary summary(ParentOrderId parent_order_id) const;
    [[nodiscard]] std::optional<Timestamp> current_time() const noexcept {
        return current_time_;
    }

private:
    struct ParentState {
        ParentOrder parent;
        std::string algorithm;
        std::size_t pending_scheduled_children;
        Quantity observed_market_volume{0};
        std::vector<ChildExecution> executions;
        std::vector<ExecutionFill> fills;
    };

    struct ScheduledEntry {
        ChildOrder child;
        bool submitted{false};
    };

    MatchingEngine& engine_;
    SchedulerConfig config_;
    std::optional<Timestamp> current_time_;
    std::unordered_map<ParentOrderId, ParentState> parents_;
    std::vector<ScheduledEntry> schedule_;
    std::unordered_set<OrderId> child_order_ids_;
    std::unordered_map<TradeId, ChildOrder> trade_to_child_;

    [[nodiscard]] ChildExecution submit_child(ParentState& state,
                                               ChildOrder child);
    void expire_at(Timestamp timestamp);
};

}  // namespace quant_engine::execution
