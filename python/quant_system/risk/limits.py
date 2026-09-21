"""Explicit pre-trade exposure-limit configuration."""

import math
from dataclasses import dataclass

from quant_system.risk.exceptions import RiskConfigurationError


@dataclass(frozen=True, slots=True)
class RiskLimits:
    max_gross_exposure: float | None = None
    max_net_exposure: float | None = None
    max_position_notional: float | None = None
    max_gross_exposure_pct: float | None = None
    max_position_pct: float | None = None

    def __post_init__(self) -> None:
        for field_name in (
            "max_gross_exposure",
            "max_net_exposure",
            "max_position_notional",
            "max_gross_exposure_pct",
            "max_position_pct",
        ):
            value = getattr(self, field_name)
            if value is None:
                continue
            if isinstance(value, bool) or not isinstance(value, int | float):
                raise RiskConfigurationError(f"{field_name} must be a real number or None")
            normalized = float(value)
            if not math.isfinite(normalized) or normalized < 0.0:
                raise RiskConfigurationError(f"{field_name} must be finite and non-negative")
            object.__setattr__(self, field_name, normalized)

