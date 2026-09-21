#include "quant_engine/derivatives/black_scholes.hpp"
#include "quant_engine/derivatives/greeks.hpp"
#include "quant_engine/derivatives/implied_volatility.hpp"

#include <iomanip>
#include <iostream>

int main() {
    using namespace quant_engine::derivatives;

    const EuropeanOption option{OptionType::Call, 100.0, 100.0, 1.0, 0.05,
                                0.20, 0.0};
    const double price = black_scholes_price(option);
    const Greeks greeks = calculate_greeks(option);
    const double recovered_volatility = implied_volatility(
        option.type, price, option.spot, option.strike, option.time_to_expiry,
        option.risk_free_rate, option.dividend_yield);

    std::cout << std::fixed << std::setprecision(6)
              << "EUROPEAN CALL\n"
              << "Spot: " << option.spot << '\n'
              << "Strike: " << option.strike << '\n'
              << "Expiry (years): " << option.time_to_expiry << '\n'
              << "Rate: " << option.risk_free_rate << '\n'
              << "Volatility: " << option.volatility << '\n'
              << "Price: " << price << '\n'
              << "Delta: " << greeks.delta << '\n'
              << "Gamma: " << greeks.gamma << '\n'
              << "Vega (per 1.0 vol): " << greeks.vega << '\n'
              << "Theta (annual): " << greeks.theta << '\n'
              << "Rho (per 1.0 rate): " << greeks.rho << '\n'
              << "Recovered IV: " << recovered_volatility << '\n';
    return 0;
}
