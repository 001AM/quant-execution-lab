#pragma once

#include "quant_engine/types.hpp"

namespace quant_engine {

struct Trade {
    TradeId trade_id;
    OrderId buy_order_id;
    OrderId sell_order_id;
    Price price;
    Quantity quantity;
    SequenceNumber sequence;

    bool operator==(const Trade&) const = default;
};

}  // namespace quant_engine
