#include "quant_engine/derivatives/greeks.hpp"

#include "quant_engine/derivatives/black_scholes.hpp"
#include "quant_engine/derivatives/normal_distribution.hpp"

#include <cmath>
#include <stdexcept>

namespace quant_engine::derivatives {

Greeks calculate_greeks(const EuropeanOption& option) {
    validate_option(option);
    if (option.time_to_expiry == 0.0 || option.volatility == 0.0) {
        throw std::domain_error(
            "Greeks are undefined at zero expiry or zero volatility");
    }

    const double sqrt_time = std::sqrt(option.time_to_expiry);
    const double d1 = black_scholes_d1(option);
    const double d2 = black_scholes_d2(option);
    const double discount_spot =
        std::exp(-option.dividend_yield * option.time_to_expiry);
    const double discount_strike =
        std::exp(-option.risk_free_rate * option.time_to_expiry);
    const double density = normal_pdf(d1);

    const double delta =
        option.type == OptionType::Call
            ? discount_spot * normal_cdf(d1)
            : discount_spot * (normal_cdf(d1) - 1.0);
    const double gamma = discount_spot * density /
                         (option.spot * option.volatility * sqrt_time);
    const double vega = option.spot * discount_spot * density * sqrt_time;

    const double diffusion_theta =
        -option.spot * discount_spot * density * option.volatility /
        (2.0 * sqrt_time);
    double theta = 0.0;
    double rho = 0.0;
    if (option.type == OptionType::Call) {
        theta = diffusion_theta -
                option.risk_free_rate * option.strike * discount_strike *
                    normal_cdf(d2) +
                option.dividend_yield * option.spot * discount_spot *
                    normal_cdf(d1);
        rho = option.strike * option.time_to_expiry * discount_strike *
              normal_cdf(d2);
    } else {
        theta = diffusion_theta +
                option.risk_free_rate * option.strike * discount_strike *
                    normal_cdf(-d2) -
                option.dividend_yield * option.spot * discount_spot *
                    normal_cdf(-d1);
        rho = -option.strike * option.time_to_expiry * discount_strike *
              normal_cdf(-d2);
    }

    return Greeks{.delta = delta,
                  .gamma = gamma,
                  .vega = vega,
                  .theta = theta,
                  .rho = rho};
}

}  // namespace quant_engine::derivatives
