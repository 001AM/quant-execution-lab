#pragma once

#include <cstdint>

namespace quant_engine::derivatives {

enum class OptionType : std::uint8_t { Call, Put };

struct EuropeanOption {
    OptionType type;
    double spot;
    double strike;
    double time_to_expiry;
    double risk_free_rate;
    double volatility;
    double dividend_yield{0.0};
};

void validate_option(const EuropeanOption& option);

}  // namespace quant_engine::derivatives
