"""Return transformations and compounded performance series."""

import math

import numpy as np
import pandas as pd


def _numeric_series(
    values: pd.Series,
    *,
    label: str,
    allow_leading_nan: bool = False,
) -> pd.Series:
    if not isinstance(values, pd.Series):
        raise TypeError(f"{label} must be a pandas Series")
    if pd.api.types.is_bool_dtype(values.dtype) or not pd.api.types.is_numeric_dtype(values.dtype):
        raise ValueError(f"{label} must contain numeric values")

    result = values.astype(float).copy()
    missing = result.isna()
    if missing.any():
        leading_nan_only = (
            allow_leading_nan
            and len(result) > 0
            and bool(missing.iloc[0])
            and int(missing.sum()) == 1
        )
        if not leading_nan_only:
            raise ValueError(
                f"{label} contains missing values; only one leading NaN is permitted"
            )

    finite_values = result[~missing].to_numpy(dtype=float)
    if not np.isfinite(finite_values).all():
        raise ValueError(f"{label} must contain only finite values")
    return result


def _validated_returns(returns: pd.Series) -> pd.Series:
    values = _numeric_series(returns, label="returns", allow_leading_nan=True)
    below_total_loss = values < -1.0
    if below_total_loss.any():
        bad_value = values[below_total_loss].iloc[0]
        raise ValueError(f"simple return {bad_value} is below -1.0 (a loss greater than 100%)")
    return values


def simple_returns(prices: pd.Series) -> pd.Series:
    """Calculate period-over-period simple returns.

    The output preserves the input index, name, and initial NaN because the
    first price has no preceding observation.
    """

    values = _numeric_series(prices, label="prices")
    result = values.pct_change(fill_method=None)
    calculated = result.iloc[1:]
    if calculated.isna().any() or not np.isfinite(calculated.to_numpy(dtype=float)).all():
        raise ValueError("simple returns are undefined when a prior price is zero")
    return result


def log_returns(prices: pd.Series) -> pd.Series:
    """Calculate period-over-period logarithmic returns.

    Prices must be strictly positive. The output preserves the initial NaN.
    """

    values = _numeric_series(prices, label="prices")
    non_positive = values <= 0.0
    if non_positive.any():
        bad_value = values[non_positive].iloc[0]
        raise ValueError(f"log returns require strictly positive prices; received {bad_value}")
    ratios = values / values.shift(1)
    return pd.Series(
        np.log(ratios.to_numpy(dtype=float)),
        index=values.index,
        name=values.name,
    )


def cumulative_returns(returns: pd.Series) -> pd.Series:
    """Compound simple returns into cumulative returns.

    A single leading NaN, such as the output of :func:`simple_returns`, is
    preserved. Missing values anywhere else are rejected.
    """

    values = _validated_returns(returns)
    return (1.0 + values).cumprod() - 1.0


def equity_curve(returns: pd.Series, initial_capital: float = 100_000.0) -> pd.Series:
    """Compound simple returns into an equity curve.

    The series contains end-of-period equity values; it does not prepend an
    extra initial-capital observation. A leading NaN in returns is preserved.
    """

    if isinstance(initial_capital, bool) or not isinstance(initial_capital, int | float):
        raise TypeError("initial_capital must be a real number")
    capital = float(initial_capital)
    if not math.isfinite(capital) or capital <= 0.0:
        raise ValueError("initial_capital must be finite and greater than zero")

    values = _validated_returns(returns)
    return capital * (1.0 + values).cumprod()
