#pragma once

#include "quant_engine/types.hpp"

#include <optional>
#include <string>

namespace quant_engine::execution {

class ParentOrder {
public:
    ParentOrder(ParentOrderId parent_order_id, std::string symbol, Side side,
                Quantity total_quantity, Timestamp start_time,
                Timestamp end_time);

    [[nodiscard]] ParentOrderId parent_order_id() const noexcept {
        return parent_order_id_;
    }
    [[nodiscard]] const std::string& symbol() const noexcept { return symbol_; }
    [[nodiscard]] Side side() const noexcept { return side_; }
    [[nodiscard]] Quantity total_quantity() const noexcept {
        return total_quantity_;
    }
    [[nodiscard]] Quantity executed_quantity() const noexcept {
        return executed_quantity_;
    }
    [[nodiscard]] Quantity remaining_quantity() const noexcept {
        return total_quantity_ - executed_quantity_;
    }
    [[nodiscard]] Timestamp start_time() const noexcept { return start_time_; }
    [[nodiscard]] Timestamp end_time() const noexcept { return end_time_; }
    [[nodiscard]] ParentOrderStatus status() const noexcept { return status_; }
    [[nodiscard]] bool is_terminal() const noexcept;
    [[nodiscard]] bool can_generate_children() const noexcept;

    void activate();
    void record_fill(Quantity quantity);
    void cancel();
    void expire();

private:
    ParentOrderId parent_order_id_;
    std::string symbol_;
    Side side_;
    Quantity total_quantity_;
    Quantity executed_quantity_{0};
    Timestamp start_time_;
    Timestamp end_time_;
    ParentOrderStatus status_{ParentOrderStatus::Pending};
};

struct ChildOrder {
    OrderId order_id;
    ParentOrderId parent_order_id;
    Side side;
    Quantity quantity;
    OrderType type;
    std::optional<Price> price;
    Timestamp scheduled_time;
    SequenceNumber schedule_sequence;

    bool operator==(const ChildOrder&) const = default;
};

struct MarketVolumeObservation {
    Timestamp timestamp;
    Quantity volume;

    bool operator==(const MarketVolumeObservation&) const = default;
};

}  // namespace quant_engine::execution
