#include "quant_engine/derivatives/black_scholes.hpp"
#include "quant_engine/derivatives/greeks.hpp"

#include "../test_support.hpp"

#include <cmath>
#include <stdexcept>

namespace {
using namespace quant_engine::derivatives;

EuropeanOption option(const OptionType type = OptionType::Call) {
    return EuropeanOption{type, 100.0, 100.0, 1.0, 0.05, 0.20, 0.0};
}

void call_delta_matches_reference() {
    CHECK(std::abs(calculate_greeks(option()).delta - 0.6368306511756191) <
          1e-10);
}

void put_delta_matches_reference() {
    CHECK(std::abs(calculate_greeks(option(OptionType::Put)).delta +
                   0.3631693488243809) < 1e-10);
}

void call_and_put_gamma_are_equal() {
    CHECK(std::abs(calculate_greeks(option()).gamma -
                   calculate_greeks(option(OptionType::Put)).gamma) < 1e-14);
}

void vega_is_per_absolute_volatility_change() {
    CHECK(std::abs(calculate_greeks(option()).vega - 37.52403469169379) <
          1e-10);
}

void call_theta_is_annual() {
    CHECK(std::abs(calculate_greeks(option()).theta + 6.414027546438197) <
          1e-10);
}

void put_theta_is_annual() {
    CHECK(std::abs(calculate_greeks(option(OptionType::Put)).theta +
                   1.657880423934626) < 1e-10);
}

void call_rho_is_per_absolute_rate_change() {
    CHECK(std::abs(calculate_greeks(option()).rho - 53.232481545376345) <
          1e-10);
}

void put_rho_is_per_absolute_rate_change() {
    CHECK(std::abs(calculate_greeks(option(OptionType::Put)).rho +
                   41.89046090469506) < 1e-10);
}

void delta_matches_central_finite_difference() {
    const auto input = option();
    const double epsilon = 1e-3;
    auto higher = input;
    auto lower = input;
    higher.spot += epsilon;
    lower.spot -= epsilon;
    const double numerical =
        (black_scholes_price(higher) - black_scholes_price(lower)) /
        (2.0 * epsilon);
    CHECK(std::abs(numerical - calculate_greeks(input).delta) < 1e-8);
}

void gamma_matches_central_finite_difference() {
    const auto input = option();
    const double epsilon = 1e-2;
    auto higher = input;
    auto lower = input;
    higher.spot += epsilon;
    lower.spot -= epsilon;
    const double numerical =
        (black_scholes_price(higher) - 2.0 * black_scholes_price(input) +
         black_scholes_price(lower)) /
        (epsilon * epsilon);
    CHECK(std::abs(numerical - calculate_greeks(input).gamma) < 1e-7);
}

void vega_matches_central_finite_difference() {
    const auto input = option();
    const double epsilon = 1e-5;
    auto higher = input;
    auto lower = input;
    higher.volatility += epsilon;
    lower.volatility -= epsilon;
    const double numerical =
        (black_scholes_price(higher) - black_scholes_price(lower)) /
        (2.0 * epsilon);
    CHECK(std::abs(numerical - calculate_greeks(input).vega) < 1e-7);
}

void zero_expiry_or_volatility_greeks_are_rejected() {
    auto expired = option();
    expired.time_to_expiry = 0.0;
    EXPECT_THROW(calculate_greeks(expired), std::domain_error);
    auto deterministic = option();
    deterministic.volatility = 0.0;
    EXPECT_THROW(calculate_greeks(deterministic), std::domain_error);
}

}  // namespace

int main() {
    call_delta_matches_reference();
    put_delta_matches_reference();
    call_and_put_gamma_are_equal();
    vega_is_per_absolute_volatility_change();
    call_theta_is_annual();
    put_theta_is_annual();
    call_rho_is_per_absolute_rate_change();
    put_rho_is_per_absolute_rate_change();
    delta_matches_central_finite_difference();
    gamma_matches_central_finite_difference();
    vega_matches_central_finite_difference();
    zero_expiry_or_volatility_greeks_are_rejected();
    return test_support::failures == 0 ? 0 : 1;
}
