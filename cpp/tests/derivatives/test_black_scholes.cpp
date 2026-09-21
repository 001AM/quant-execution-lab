#include "quant_engine/derivatives/black_scholes.hpp"

#include "../test_support.hpp"

#include <cmath>
#include <stdexcept>

namespace {
using namespace quant_engine::derivatives;

EuropeanOption option(const OptionType type, const double spot = 100.0,
                      const double strike = 100.0) {
    return EuropeanOption{type, spot, strike, 1.0, 0.05, 0.20, 0.0};
}

void atm_call_matches_reference_value() {
    CHECK(std::abs(black_scholes_price(option(OptionType::Call)) -
                   10.450583572185565) < 1e-10);
}

void atm_put_matches_reference_value() {
    CHECK(std::abs(black_scholes_price(option(OptionType::Put)) -
                   5.573526022256971) < 1e-10);
}

void itm_call_exceeds_otm_call() {
    CHECK(black_scholes_price(option(OptionType::Call, 120.0)) >
          black_scholes_price(option(OptionType::Call, 80.0)));
}

void itm_put_exceeds_otm_put() {
    CHECK(black_scholes_price(option(OptionType::Put, 80.0)) >
          black_scholes_price(option(OptionType::Put, 120.0)));
}

void put_call_parity_holds_with_dividends() {
    auto call = option(OptionType::Call);
    call.dividend_yield = 0.02;
    auto put = call;
    put.type = OptionType::Put;
    const double lhs = black_scholes_price(call) - black_scholes_price(put);
    const double rhs = call.spot * std::exp(-call.dividend_yield) -
                       call.strike * std::exp(-call.risk_free_rate);
    CHECK(std::abs(lhs - rhs) < 1e-12);
}

void zero_expiry_returns_intrinsic_value() {
    auto call = option(OptionType::Call, 110.0, 100.0);
    call.time_to_expiry = 0.0;
    auto put = option(OptionType::Put, 90.0, 100.0);
    put.time_to_expiry = 0.0;
    CHECK(black_scholes_price(call) == 10.0);
    CHECK(black_scholes_price(put) == 10.0);
}

void zero_volatility_uses_discounted_deterministic_payoff() {
    auto call = option(OptionType::Call);
    call.volatility = 0.0;
    const double expected = 100.0 - 100.0 * std::exp(-0.05);
    CHECK(std::abs(black_scholes_price(call) - expected) < 1e-12);
}

void dividend_yield_reduces_call_value() {
    auto without_dividend = option(OptionType::Call);
    auto with_dividend = without_dividend;
    with_dividend.dividend_yield = 0.03;
    CHECK(black_scholes_price(with_dividend) <
          black_scholes_price(without_dividend));
}

void negative_interest_rate_is_supported() {
    auto call = option(OptionType::Call);
    call.risk_free_rate = -0.01;
    CHECK(std::isfinite(black_scholes_price(call)));
    CHECK(black_scholes_price(call) > 0.0);
}

void deep_options_remain_finite() {
    for (const auto type : {OptionType::Call, OptionType::Put}) {
        CHECK(std::isfinite(black_scholes_price(option(type, 20.0, 100.0))));
        CHECK(std::isfinite(black_scholes_price(option(type, 500.0, 100.0))));
    }
}

void short_expiry_and_high_volatility_are_stable() {
    auto input = option(OptionType::Call);
    input.time_to_expiry = 1.0 / 365.0;
    input.volatility = 1.5;
    CHECK(std::isfinite(black_scholes_price(input)));
    CHECK(black_scholes_price(input) >= 0.0);
}

void convenience_functions_match_generic_pricer() {
    CHECK(std::abs(black_scholes_call_price(100, 100, 1, 0.05, 0.20) -
                   black_scholes_price(option(OptionType::Call))) < 1e-12);
    CHECK(std::abs(black_scholes_put_price(100, 100, 1, 0.05, 0.20) -
                   black_scholes_price(option(OptionType::Put))) < 1e-12);
}

void invalid_spot_is_rejected() {
    EXPECT_THROW(black_scholes_call_price(0, 100, 1, 0.05, 0.20),
                 std::invalid_argument);
}

void invalid_strike_and_expiry_are_rejected() {
    EXPECT_THROW(black_scholes_call_price(100, 0, 1, 0.05, 0.20),
                 std::invalid_argument);
    EXPECT_THROW(black_scholes_call_price(100, 100, -1, 0.05, 0.20),
                 std::invalid_argument);
}

}  // namespace

int main() {
    atm_call_matches_reference_value();
    atm_put_matches_reference_value();
    itm_call_exceeds_otm_call();
    itm_put_exceeds_otm_put();
    put_call_parity_holds_with_dividends();
    zero_expiry_returns_intrinsic_value();
    zero_volatility_uses_discounted_deterministic_payoff();
    dividend_yield_reduces_call_value();
    negative_interest_rate_is_supported();
    deep_options_remain_finite();
    short_expiry_and_high_volatility_are_stable();
    convenience_functions_match_generic_pricer();
    invalid_spot_is_rejected();
    invalid_strike_and_expiry_are_rejected();
    return test_support::failures == 0 ? 0 : 1;
}
