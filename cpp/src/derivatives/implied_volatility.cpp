#include "quant_engine/derivatives/implied_volatility.hpp"

#include "quant_engine/derivatives/black_scholes.hpp"
#include "quant_engine/derivatives/greeks.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace quant_engine::derivatives {
namespace {

EuropeanOption make_option(const OptionType type, const double spot,
                           const double strike, const double time_to_expiry,
                           const double risk_free_rate,
                           const double volatility,
                           const double dividend_yield) {
    return EuropeanOption{.type = type,
                          .spot = spot,
                          .strike = strike,
                          .time_to_expiry = time_to_expiry,
                          .risk_free_rate = risk_free_rate,
                          .volatility = volatility,
                          .dividend_yield = dividend_yield};
}

void validate_config(const ImpliedVolatilityConfig& config) {
    if (!std::isfinite(config.initial_guess) ||
        !std::isfinite(config.minimum_volatility) ||
        !std::isfinite(config.maximum_volatility) ||
        config.minimum_volatility <= 0.0 ||
        config.maximum_volatility <= config.minimum_volatility ||
        !std::isfinite(config.price_tolerance) ||
        config.price_tolerance <= 0.0 || !std::isfinite(config.vega_floor) ||
        config.vega_floor <= 0.0) {
        throw std::invalid_argument("invalid implied-volatility configuration");
    }
}

void validate_market_price(const OptionType type, const double market_price,
                           const EuropeanOption& option,
                           const ImpliedVolatilityConfig& config) {
    if (!std::isfinite(market_price) || market_price < 0.0) {
        throw std::invalid_argument(
            "market option price must be finite and non-negative");
    }
    if (option.time_to_expiry <= 0.0) {
        throw std::invalid_argument(
            "implied volatility requires positive time to expiry");
    }
    const double discounted_spot =
        option.spot *
        std::exp(-option.dividend_yield * option.time_to_expiry);
    const double discounted_strike =
        option.strike *
        std::exp(-option.risk_free_rate * option.time_to_expiry);
    const double lower =
        type == OptionType::Call
            ? std::max(discounted_spot - discounted_strike, 0.0)
            : std::max(discounted_strike - discounted_spot, 0.0);
    const double upper =
        type == OptionType::Call ? discounted_spot : discounted_strike;
    if (market_price < lower - config.price_tolerance ||
        market_price > upper + config.price_tolerance) {
        throw std::invalid_argument(
            "market option price violates European arbitrage bounds");
    }
}

}  // namespace

double implied_volatility(const OptionType type, const double market_price,
                          const double spot, const double strike,
                          const double time_to_expiry,
                          const double risk_free_rate,
                          const double dividend_yield) {
    return implied_volatility(type, market_price, spot, strike, time_to_expiry,
                              risk_free_rate, dividend_yield,
                              ImpliedVolatilityConfig{});
}

double implied_volatility(const OptionType type, const double market_price,
                          const double spot, const double strike,
                          const double time_to_expiry,
                          const double risk_free_rate,
                          const double dividend_yield,
                          const ImpliedVolatilityConfig& config) {
    validate_config(config);
    auto option = make_option(type, spot, strike, time_to_expiry,
                              risk_free_rate, config.minimum_volatility,
                              dividend_yield);
    validate_option(option);
    validate_market_price(type, market_price, option, config);

    double volatility = std::clamp(config.initial_guess,
                                   config.minimum_volatility,
                                   config.maximum_volatility);
    for (std::size_t iteration = 0;
         iteration < config.newton_max_iterations; ++iteration) {
        option.volatility = volatility;
        const double difference = black_scholes_price(option) - market_price;
        if (std::abs(difference) <= config.price_tolerance) {
            return volatility;
        }
        const double vega = calculate_greeks(option).vega;
        if (!std::isfinite(vega) || std::abs(vega) < config.vega_floor) {
            break;
        }
        const double candidate = volatility - difference / vega;
        if (!std::isfinite(candidate) ||
            candidate <= config.minimum_volatility ||
            candidate >= config.maximum_volatility) {
            break;
        }
        volatility = candidate;
    }

    double lower_volatility = config.minimum_volatility;
    double upper_volatility = config.maximum_volatility;
    option.volatility = lower_volatility;
    const double lower_difference = black_scholes_price(option) - market_price;
    if (std::abs(lower_difference) <= config.price_tolerance) {
        return lower_volatility;
    }
    option.volatility = upper_volatility;
    const double upper_difference = black_scholes_price(option) - market_price;
    if (std::abs(upper_difference) <= config.price_tolerance) {
        return upper_volatility;
    }
    if (lower_difference > 0.0 || upper_difference < 0.0) {
        throw std::invalid_argument(
            "market price has no implied volatility within configured bounds");
    }

    for (std::size_t iteration = 0;
         iteration < config.bisection_max_iterations; ++iteration) {
        const double midpoint = 0.5 * (lower_volatility + upper_volatility);
        option.volatility = midpoint;
        const double difference = black_scholes_price(option) - market_price;
        if (std::abs(difference) <= config.price_tolerance) {
            return midpoint;
        }
        if (difference < 0.0) {
            lower_volatility = midpoint;
        } else {
            upper_volatility = midpoint;
        }
    }

    throw std::runtime_error("implied-volatility solver did not converge");
}

}  // namespace quant_engine::derivatives
