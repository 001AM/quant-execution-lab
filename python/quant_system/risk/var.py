"""Positive-loss Value at Risk and Expected Shortfall measures."""

import math
from statistics import NormalDist

import numpy as np
import pandas as pd

from quant_system.risk.analytics import _validated_series


def _confidence_level(confidence: float) -> float:
    if isinstance(confidence, bool) or not isinstance(confidence, int | float):
        raise TypeError("confidence must be a real number")
    value = float(confidence)
    if not math.isfinite(value) or not 0.0 < value < 1.0:
        raise ValueError("confidence must be strictly between 0 and 1")
    return value


def historical_var(returns: pd.Series, confidence: float = 0.95) -> float:
    """Return empirical linear-quantile VaR as a positive loss magnitude."""

    confidence_level = _confidence_level(confidence)
    values = _validated_series(returns, name="returns")
    cutoff = float(
        np.quantile(values.to_numpy(dtype=float), 1.0 - confidence_level, method="linear")
    )
    return max(0.0, -cutoff)


def parametric_var(returns: pd.Series, confidence: float = 0.95) -> float:
    """Return Gaussian VaR using sample standard deviation as positive loss."""

    confidence_level = _confidence_level(confidence)
    values = _validated_series(returns, name="returns", minimum_observations=2)
    lower_tail_z = NormalDist().inv_cdf(1.0 - confidence_level)
    cutoff = float(values.mean()) + lower_tail_z * float(values.std(ddof=1))
    return max(0.0, -cutoff)


def expected_shortfall(returns: pd.Series, confidence: float = 0.95) -> float:
    """Return average loss at or below the historical VaR quantile cutoff."""

    confidence_level = _confidence_level(confidence)
    values = _validated_series(returns, name="returns")
    cutoff = float(
        np.quantile(values.to_numpy(dtype=float), 1.0 - confidence_level, method="linear")
    )
    tail = values[values <= cutoff]
    if tail.empty:
        raise ValueError("expected shortfall tail contains no observations")
    return max(0.0, -float(tail.mean()))


def var_amount(portfolio_value: float, var_fraction: float) -> float:
    """Convert a non-negative VaR fraction into a monetary loss amount."""

    for name, value in (("portfolio_value", portfolio_value), ("var_fraction", var_fraction)):
        if isinstance(value, bool) or not isinstance(value, int | float):
            raise TypeError(f"{name} must be a real number")
        if not math.isfinite(float(value)) or value < 0.0:
            raise ValueError(f"{name} must be finite and non-negative")
    return float(portfolio_value * var_fraction)
