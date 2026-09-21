#pragma once

#include "quant_engine/derivatives/option.hpp"

#include <cstddef>

namespace quant_engine::derivatives {

struct ImpliedVolatilityConfig {
    double initial_guess{0.20};
    double minimum_volatility{1.0e-8};
    double maximum_volatility{5.0};
    double price_tolerance{1.0e-10};
    double vega_floor{1.0e-12};
    std::size_t newton_max_iterations{100};
    std::size_t bisection_max_iterations{200};
};

[[nodiscard]] double implied_volatility(
    OptionType type, double market_price, double spot, double strike,
    double time_to_expiry, double risk_free_rate,
    double dividend_yield = 0.0);

[[nodiscard]] double implied_volatility(
    OptionType type, double market_price, double spot, double strike,
    double time_to_expiry, double risk_free_rate, double dividend_yield,
    const ImpliedVolatilityConfig& config);

}  // namespace quant_engine::derivatives
