#pragma once

#include "quant_engine/price_level.hpp"
#include "quant_engine/execution_report.hpp"
#include "quant_engine/trade.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace quant_engine {

struct LevelSnapshot {
    Price price;
    Quantity total_quantity;
    std::size_t order_count;

    bool operator==(const LevelSnapshot&) const = default;
};

struct BookSnapshot {
    std::vector<LevelSnapshot> bids;
    std::vector<LevelSnapshot> asks;

    bool operator==(const BookSnapshot&) const = default;
};

struct ReplaceOutcome {
    std::vector<Trade> trades;
    bool priority_preserved;
};

class OrderBook {
public:
    void reserve_capacity(std::size_t expected_active_orders,
                          std::size_t expected_completed_orders,
                          std::size_t expected_trades,
                          std::size_t expected_orders_per_level);
    [[nodiscard]] std::vector<Trade> add_limit_order(
        OrderId order_id, Side side, Price price, Quantity quantity,
        Timestamp timestamp = Timestamp{});
    [[nodiscard]] std::vector<Trade> add_market_order(
        OrderId order_id, Side side, Quantity quantity,
        Timestamp timestamp = Timestamp{});
    [[nodiscard]] bool cancel_order(OrderId order_id);
    [[nodiscard]] ReplaceOutcome replace_order(
        OrderId order_id, Price new_price, Quantity new_total_quantity);

    [[nodiscard]] std::optional<Price> best_bid() const noexcept;
    [[nodiscard]] std::optional<Price> best_ask() const noexcept;
    [[nodiscard]] std::optional<Price> spread() const noexcept;
    [[nodiscard]] std::optional<double> mid_price() const noexcept;
    [[nodiscard]] bool is_crossed() const noexcept;

    [[nodiscard]] std::size_t active_order_count() const noexcept {
        return active_orders_.size();
    }
    [[nodiscard]] const Order* find_active_order(OrderId order_id) const noexcept;
    [[nodiscard]] const Order* find_order(OrderId order_id) const noexcept;
    [[nodiscard]] std::optional<OrderView> get_order(OrderId order_id) const;
    [[nodiscard]] bool has_used_order_id(OrderId order_id) const noexcept;
    [[nodiscard]] const std::vector<Trade>& trades() const noexcept { return trades_; }
    [[nodiscard]] BookSnapshot snapshot() const;
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] bool validate_invariants() const noexcept;

private:
    using BidLevels = std::map<Price, PriceLevel, std::greater<Price>>;
    using AskLevels = std::map<Price, PriceLevel>;

    SequenceNumber next_order_sequence_{1};
    SequenceNumber next_trade_sequence_{1};
    BidLevels bids_;
    AskLevels asks_;
    struct ActiveOrder {
        std::unique_ptr<Order> order;
        PriceLevel* level{nullptr};
    };

    std::unordered_map<OrderId, ActiveOrder> active_orders_;
    std::unordered_map<OrderId, Order> completed_orders_;
    std::vector<Trade> trades_;
    std::size_t expected_orders_per_level_{0};

    void match_buy(Order& incoming, std::vector<Trade>& generated);
    void match_sell(Order& incoming, std::vector<Trade>& generated);
    void rest(Order& order);
    void remove_from_level(Order& order);
    void complete_order(OrderId order_id);
    void record_trade(Order& incoming, Order& resting, Price execution_price,
                      Quantity quantity, std::vector<Trade>& generated);
};

}  // namespace quant_engine
