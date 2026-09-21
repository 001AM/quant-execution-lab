#pragma once

#include "quant_engine/types.hpp"

#include <optional>

namespace quant_engine {

class Order {
public:
    Order(OrderId order_id, Side side, OrderType type, Price price,
          Quantity quantity, SequenceNumber sequence,
          Timestamp timestamp = Timestamp{});

    [[nodiscard]] OrderId order_id() const noexcept { return order_id_; }
    [[nodiscard]] Side side() const noexcept { return side_; }
    [[nodiscard]] OrderType type() const noexcept { return type_; }
    [[nodiscard]] Price price() const noexcept { return price_; }
    [[nodiscard]] Quantity original_quantity() const noexcept {
        return original_quantity_;
    }
    [[nodiscard]] Quantity remaining_quantity() const noexcept {
        return remaining_quantity_;
    }
    [[nodiscard]] Quantity filled_quantity() const noexcept {
        return original_quantity_ - remaining_quantity_;
    }
    [[nodiscard]] SequenceNumber sequence() const noexcept { return sequence_; }
    [[nodiscard]] OrderStatus status() const noexcept { return status_; }
    [[nodiscard]] Timestamp timestamp() const noexcept { return timestamp_; }
    [[nodiscard]] bool is_active() const noexcept;
    [[nodiscard]] std::optional<double> average_execution_price() const noexcept;
    [[nodiscard]] std::optional<Price> last_execution_price() const noexcept {
        return last_execution_price_;
    }
    [[nodiscard]] std::optional<Quantity> last_execution_quantity() const noexcept {
        return last_execution_quantity_;
    }

    void activate();
    void fill(Quantity quantity);
    void fill(Quantity quantity, Price execution_price);
    void cancel();
    void replace(Price price, Quantity total_quantity, SequenceNumber sequence);

private:
    OrderId order_id_;
    Side side_;
    OrderType type_;
    Price price_;
    Quantity original_quantity_;
    Quantity remaining_quantity_;
    SequenceNumber sequence_;
    OrderStatus status_{OrderStatus::New};
    Timestamp timestamp_;
    long double executed_notional_{0.0L};
    std::optional<Price> last_execution_price_;
    std::optional<Quantity> last_execution_quantity_;
};

}  // namespace quant_engine
