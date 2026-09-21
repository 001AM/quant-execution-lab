#pragma once

#include "quant_engine/types.hpp"

#include <optional>
#include <vector>

namespace quant_engine::analytics {

struct MarketTradeObservation {
    Price price;
    Quantity volume;
    Timestamp timestamp{};
};

struct ExecutionBenchmark {
    Price arrival_price;
    std::optional<Price> decision_price;
    std::optional<Price> final_market_price;
    std::optional<Price> arrival_bid;
    std::optional<Price> arrival_ask;
    std::vector<MarketTradeObservation> market_trades;
    double fees{0.0};
    double tick_value{1.0};
};

}  // namespace quant_engine::analytics
