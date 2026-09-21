#pragma once

#include "quant_engine/order_book.hpp"
#include "quant_engine/result.hpp"

#include <optional>
#include <unordered_map>

namespace quant_engine {

class MatchingEngine {
public:
    void reserve_capacity(std::size_t expected_active_orders,
                          std::size_t expected_completed_orders,
                          std::size_t expected_trades,
                          std::size_t expected_orders_per_level);
    [[nodiscard]] SubmitResult submit(const OrderRequest& request);
    [[nodiscard]] CancelResult cancel(OrderId order_id);
    [[nodiscard]] ModifyResult modify(OrderId order_id,
                                      const ModifyRequest& request);

    [[nodiscard]] const OrderBook& book() const noexcept { return book_; }
    [[nodiscard]] std::optional<OrderView> get_order(OrderId order_id) const;
    [[nodiscard]] const std::vector<Trade>& trades() const noexcept {
        return book_.trades();
    }

private:
    OrderBook book_;
    std::unordered_map<OrderId, OrderView> rejected_orders_;

    [[nodiscard]] static ExecutionReport report_for(const Order& order);
    [[nodiscard]] static ExecutionReport rejection_report(
        const OrderRequest& request);
    [[nodiscard]] static ExecutionReport report_for(const OrderView& order);
};

}  // namespace quant_engine
