#pragma once

namespace quant_engine::derivatives {

[[nodiscard]] double normal_pdf(double value) noexcept;
[[nodiscard]] double normal_cdf(double value) noexcept;

}  // namespace quant_engine::derivatives
