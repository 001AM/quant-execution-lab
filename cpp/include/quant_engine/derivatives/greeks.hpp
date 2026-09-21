#pragma once

#include "quant_engine/derivatives/option.hpp"

namespace quant_engine::derivatives {

struct Greeks {
    double delta;
    double gamma;
    double vega;
    double theta;
    double rho;
};

// Vega and rho are per absolute 1.0 change. Theta is annual.
[[nodiscard]] Greeks calculate_greeks(const EuropeanOption& option);

}  // namespace quant_engine::derivatives
