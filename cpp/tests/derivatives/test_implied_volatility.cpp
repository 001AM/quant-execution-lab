#include "quant_engine/derivatives/black_scholes.hpp"
#include "quant_engine/derivatives/implied_volatility.hpp"

#include "../test_support.hpp"

#include <cmath>
#include <stdexcept>

namespace {
using namespace quant_engine::derivatives;

double round_trip(const OptionType type, const double spot, const double strike,
                  const double expiry, const double volatility,
                  const double rate = 0.03, const double dividend = 0.0) {
    const EuropeanOption option{type, spot, strike, expiry, rate, volatility,
                                dividend};
    return implied_volatility(type, black_scholes_price(option), spot, strike,
                              expiry, rate, dividend);
}

void call_round_trip_recovers_twenty_percent() {
    CHECK(std::abs(round_trip(OptionType::Call, 100, 100, 1, 0.20) - 0.20) <
          1e-9);
}

void put_round_trip_recovers_twenty_percent() {
    CHECK(std::abs(round_trip(OptionType::Put, 100, 100, 1, 0.20) - 0.20) <
          1e-9);
}

void low_volatility_round_trip() {
    CHECK(std::abs(round_trip(OptionType::Call, 100, 105, 2, 0.05) - 0.05) <
          1e-8);
}

void high_volatility_round_trip() {
    CHECK(std::abs(round_trip(OptionType::Put, 100, 90, 1.5, 1.25) - 1.25) <
          1e-9);
}

void short_expiry_round_trip() {
    CHECK(std::abs(round_trip(OptionType::Call, 100, 100, 1.0 / 365.0, 0.35) -
                   0.35) < 1e-8);
}

void itm_and_otm_round_trips() {
    CHECK(std::abs(round_trip(OptionType::Call, 120, 100, 1, 0.30) - 0.30) <
          1e-8);
    CHECK(std::abs(round_trip(OptionType::Put, 120, 100, 1, 0.30) - 0.30) <
          1e-8);
}

void dividend_round_trip() {
    CHECK(std::abs(round_trip(OptionType::Call, 100, 95, 1, 0.22, 0.03,
                              0.02) -
                   0.22) < 1e-9);
}

void newton_converges_from_distant_initial_guess() {
    const EuropeanOption option{OptionType::Call, 100, 100, 1, 0.05, 0.60, 0};
    ImpliedVolatilityConfig config;
    config.initial_guess = 0.05;
    const double recovered = implied_volatility(
        option.type, black_scholes_price(option), option.spot, option.strike,
        option.time_to_expiry, option.risk_free_rate, option.dividend_yield,
        config);
    CHECK(std::abs(recovered - 0.60) < 1e-9);
}

void bisection_fallback_converges_when_newton_is_disabled() {
    const EuropeanOption option{OptionType::Put, 100, 110, 2, 0.01, 0.42, 0};
    ImpliedVolatilityConfig config;
    config.newton_max_iterations = 0;
    const double recovered = implied_volatility(
        option.type, black_scholes_price(option), option.spot, option.strike,
        option.time_to_expiry, option.risk_free_rate, option.dividend_yield,
        config);
    CHECK(std::abs(recovered - 0.42) < 1e-8);
}

void impossible_market_price_is_rejected() {
    EXPECT_THROW(implied_volatility(OptionType::Call, 101, 100, 100, 1, 0),
                 std::invalid_argument);
    EXPECT_THROW(implied_volatility(OptionType::Put, -1, 100, 100, 1, 0),
                 std::invalid_argument);
}

void zero_expiry_is_rejected() {
    EXPECT_THROW(implied_volatility(OptionType::Call, 10, 110, 100, 0, 0),
                 std::invalid_argument);
}

void bounded_solver_rejects_root_outside_bounds() {
    const EuropeanOption option{OptionType::Call, 100, 100, 1, 0.05, 1.0, 0};
    ImpliedVolatilityConfig config;
    config.maximum_volatility = 0.50;
    EXPECT_THROW(implied_volatility(
                     option.type, black_scholes_price(option), option.spot,
                     option.strike, option.time_to_expiry,
                     option.risk_free_rate, option.dividend_yield, config),
                 std::invalid_argument);
}

void max_iteration_failure_is_explicit() {
    const EuropeanOption option{OptionType::Call, 100, 100, 1, 0.05, 0.30, 0};
    ImpliedVolatilityConfig config;
    config.newton_max_iterations = 0;
    config.bisection_max_iterations = 0;
    EXPECT_THROW(implied_volatility(
                     option.type, black_scholes_price(option), option.spot,
                     option.strike, option.time_to_expiry,
                     option.risk_free_rate, option.dividend_yield, config),
                 std::runtime_error);
}

void tiny_vega_path_falls_back_without_nan() {
    const EuropeanOption option{OptionType::Call, 100, 100, 0.01, 0.01, 0.40,
                                0};
    ImpliedVolatilityConfig config;
    config.vega_floor = 1e6;
    const double recovered = implied_volatility(
        option.type, black_scholes_price(option), option.spot, option.strike,
        option.time_to_expiry, option.risk_free_rate, option.dividend_yield,
        config);
    CHECK(std::abs(recovered - 0.40) < 1e-8);
}

}  // namespace

int main() {
    call_round_trip_recovers_twenty_percent();
    put_round_trip_recovers_twenty_percent();
    low_volatility_round_trip();
    high_volatility_round_trip();
    short_expiry_round_trip();
    itm_and_otm_round_trips();
    dividend_round_trip();
    newton_converges_from_distant_initial_guess();
    bisection_fallback_converges_when_newton_is_disabled();
    impossible_market_price_is_rejected();
    zero_expiry_is_rejected();
    bounded_solver_rejects_root_outside_bounds();
    max_iteration_failure_is_explicit();
    tiny_vega_path_falls_back_without_nan();
    return test_support::failures == 0 ? 0 : 1;
}
