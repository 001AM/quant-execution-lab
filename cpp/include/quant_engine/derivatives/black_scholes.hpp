#pragma once

#include "quant_engine/derivatives/option.hpp"

namespace quant_engine::derivatives {

[[nodiscard]] double black_scholes_d1(const EuropeanOption& option);
[[nodiscard]] double black_scholes_d2(const EuropeanOption& option);
[[nodiscard]] double black_scholes_price(const EuropeanOption& option);
[[nodiscard]] double black_scholes_call_price(
    double spot, double strike, double time_to_expiry, double risk_free_rate,
    double volatility, double dividend_yield = 0.0);
[[nodiscard]] double black_scholes_put_price(
    double spot, double strike, double time_to_expiry, double risk_free_rate,
    double volatility, double dividend_yield = 0.0);

}  // namespace quant_engine::derivatives
