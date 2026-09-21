#pragma once

#include <chrono>
#include <cstdint>

namespace quant_engine {

// Prices are integer ticks. With a 0.01 currency tick, 10025 represents 100.25.
using Price = std::int64_t;
using Quantity = std::uint64_t;
using OrderId = std::uint64_t;
using ParentOrderId = std::uint64_t;
using TradeId = std::uint64_t;
using SequenceNumber = std::uint64_t;
using Timestamp = std::chrono::system_clock::time_point;

enum class Side : std::uint8_t { Buy, Sell };
enum class OrderType : std::uint8_t { Limit, Market };
enum class OrderStatus : std::uint8_t {
    New,
    Active,
    PartiallyFilled,
    Filled,
    Cancelled,
    Rejected,
};

enum class RejectReason : std::uint8_t {
    None,
    DuplicateOrderId,
    InvalidOrderId,
    InvalidSide,
    InvalidQuantity,
    InvalidPrice,
    InvalidOrderType,
    MissingLimitPrice,
    UnexpectedMarketPrice,
    OrderNotFound,
    OrderNotActive,
    InvalidModification,
    QuantityBelowExecuted,
};

enum class ParentOrderStatus : std::uint8_t {
    Pending,
    Active,
    PartiallyFilled,
    Filled,
    Cancelled,
    Expired,
};

}  // namespace quant_engine
