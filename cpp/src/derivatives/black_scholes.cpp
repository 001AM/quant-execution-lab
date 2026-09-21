#include "quant_engine/derivatives/black_scholes.hpp"

#include "quant_engine/derivatives/normal_distribution.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace quant_engine::derivatives {

double normal_pdf(const double value) noexcept {
    const double inverse_sqrt_two_pi =
        1.0 / std::sqrt(2.0 * std::numbers::pi);
    return inverse_sqrt_two_pi * std::exp(-0.5 * value * value);
}

double normal_cdf(const double value) noexcept {
    return 0.5 * std::erfc(-value / std::sqrt(2.0));
}

void validate_option(const EuropeanOption& option) {
    if (option.type != OptionType::Call && option.type != OptionType::Put) {
        throw std::invalid_argument("option type must be Call or Put");
    }
    if (!std::isfinite(option.spot) || option.spot <= 0.0) {
        throw std::invalid_argument("spot must be finite and positive");
    }
    if (!std::isfinite(option.strike) || option.strike <= 0.0) {
        throw std::invalid_argument("strike must be finite and positive");
    }
    if (!std::isfinite(option.time_to_expiry) ||
        option.time_to_expiry < 0.0) {
        throw std::invalid_argument(
            "time to expiry must be finite and non-negative");
    }
    if (!std::isfinite(option.volatility) || option.volatility < 0.0) {
        throw std::invalid_argument(
            "volatility must be finite and non-negative");
    }
    if (!std::isfinite(option.risk_free_rate) ||
        !std::isfinite(option.dividend_yield)) {
        throw std::invalid_argument("rates must be finite");
    }
}

double black_scholes_d1(const EuropeanOption& option) {
    validate_option(option);
    if (option.time_to_expiry == 0.0 || option.volatility == 0.0) {
        throw std::domain_error(
            "d1 is undefined at zero expiry or zero volatility");
    }
    const double sigma_sqrt_time =
        option.volatility * std::sqrt(option.time_to_expiry);
    return (std::log(option.spot / option.strike) +
            (option.risk_free_rate - option.dividend_yield +
             0.5 * option.volatility * option.volatility) *
                option.time_to_expiry) /
           sigma_sqrt_time;
}

double black_scholes_d2(const EuropeanOption& option) {
    return black_scholes_d1(option) -
           option.volatility * std::sqrt(option.time_to_expiry);
}

double black_scholes_price(const EuropeanOption& option) {
    validate_option(option);
    if (option.time_to_expiry == 0.0) {
        return option.type == OptionType::Call
                   ? std::max(option.spot - option.strike, 0.0)
                   : std::max(option.strike - option.spot, 0.0);
    }

    const double discounted_spot =
        option.spot *
        std::exp(-option.dividend_yield * option.time_to_expiry);
    const double discounted_strike =
        option.strike *
        std::exp(-option.risk_free_rate * option.time_to_expiry);
    if (option.volatility == 0.0) {
        return option.type == OptionType::Call
                   ? std::max(discounted_spot - discounted_strike, 0.0)
                   : std::max(discounted_strike - discounted_spot, 0.0);
    }

    const double d1 = black_scholes_d1(option);
    const double d2 = black_scholes_d2(option);
    if (option.type == OptionType::Call) {
        return discounted_spot * normal_cdf(d1) -
               discounted_strike * normal_cdf(d2);
    }
    return discounted_strike * normal_cdf(-d2) -
           discounted_spot * normal_cdf(-d1);
}

double black_scholes_call_price(const double spot, const double strike,
                                const double time_to_expiry,
                                const double risk_free_rate,
                                const double volatility,
                                const double dividend_yield) {
    return black_scholes_price(EuropeanOption{
        .type = OptionType::Call,
        .spot = spot,
        .strike = strike,
        .time_to_expiry = time_to_expiry,
        .risk_free_rate = risk_free_rate,
        .volatility = volatility,
        .dividend_yield = dividend_yield,
    });
}

double black_scholes_put_price(const double spot, const double strike,
                               const double time_to_expiry,
                               const double risk_free_rate,
                               const double volatility,
                               const double dividend_yield) {
    return black_scholes_price(EuropeanOption{
        .type = OptionType::Put,
        .spot = spot,
        .strike = strike,
        .time_to_expiry = time_to_expiry,
        .risk_free_rate = risk_free_rate,
        .volatility = volatility,
        .dividend_yield = dividend_yield,
    });
}

}  // namespace quant_engine::derivatives
