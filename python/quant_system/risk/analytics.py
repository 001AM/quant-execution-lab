"""Statistical return, benchmark, rolling, and drawdown analytics."""

import math
from dataclasses import dataclass

import numpy as np
import pandas as pd


def _positive_integer(value: int, *, name: str, minimum: int = 1) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise TypeError(f"{name} must be an integer")
    if value < minimum:
        raise ValueError(f"{name} must be at least {minimum}")
    return value


def _validated_series(
    values: pd.Series,
    *,
    name: str,
    minimum_observations: int = 1,
) -> pd.Series:
    if not isinstance(values, pd.Series):
        raise TypeError(f"{name} must be a pandas Series")
    if pd.api.types.is_bool_dtype(values.dtype) or not pd.api.types.is_numeric_dtype(values.dtype):
        raise ValueError(f"{name} must contain numeric values")
    result = values.astype(float).copy()
    if len(result) < minimum_observations:
        raise ValueError(f"{name} requires at least {minimum_observations} observations")
    if result.isna().any():
        raise ValueError(f"{name} must not contain NaN values")
    if not np.isfinite(result.to_numpy(dtype=float)).all():
        raise ValueError(f"{name} must contain only finite values")
    return result


def _aligned_returns(
    asset_returns: pd.Series,
    benchmark_returns: pd.Series,
    *,
    minimum_observations: int = 2,
) -> tuple[pd.Series, pd.Series]:
    asset = _validated_series(asset_returns, name="asset_returns")
    benchmark = _validated_series(benchmark_returns, name="benchmark_returns")
    aligned = pd.concat(
        [asset.rename("asset"), benchmark.rename("benchmark")],
        axis=1,
        join="inner",
    )
    if len(aligned) < minimum_observations:
        raise ValueError(
            f"aligned returns require at least {minimum_observations} observations"
        )
    return aligned["asset"], aligned["benchmark"]


def _periodic_risk_free_rate(risk_free_rate: float, periods_per_year: int) -> float:
    if isinstance(risk_free_rate, bool) or not isinstance(risk_free_rate, int | float):
        raise TypeError("risk_free_rate must be a real number")
    rate = float(risk_free_rate)
    if not math.isfinite(rate) or rate <= -1.0:
        raise ValueError("risk_free_rate must be finite and greater than -1.0")
    return float((1.0 + rate) ** (1.0 / periods_per_year) - 1.0)


def rolling_volatility(
    returns: pd.Series,
    window: int = 20,
    periods_per_year: int = 252,
) -> pd.Series:
    """Return sample rolling standard deviation annualized by square-root time."""

    window_size = _positive_integer(window, name="window", minimum=2)
    periods = _positive_integer(periods_per_year, name="periods_per_year")
    values = _validated_series(returns, name="returns")
    return values.rolling(window_size, min_periods=window_size).std(ddof=1) * math.sqrt(periods)


def beta(asset_returns: pd.Series, benchmark_returns: pd.Series) -> float:
    """Calculate index-aligned sample covariance divided by benchmark variance."""

    asset, benchmark = _aligned_returns(asset_returns, benchmark_returns)
    benchmark_variance = float(benchmark.var(ddof=1))
    if benchmark_variance == 0.0:
        raise ValueError("beta is undefined when benchmark variance is zero")
    return float(asset.cov(benchmark) / benchmark_variance)


def alpha(
    asset_returns: pd.Series,
    benchmark_returns: pd.Series,
    risk_free_rate: float = 0.0,
    periods_per_year: int = 252,
) -> float:
    """Calculate arithmetic annualized CAPM intercept.

    The annual effective risk-free rate is geometrically converted to a
    periodic rate before estimating the periodic intercept.
    """

    periods = _positive_integer(periods_per_year, name="periods_per_year")
    asset, benchmark = _aligned_returns(asset_returns, benchmark_returns)
    periodic_risk_free = _periodic_risk_free_rate(risk_free_rate, periods)
    beta_value = beta(asset, benchmark)
    periodic_alpha = float(asset.mean()) - periodic_risk_free - beta_value * (
        float(benchmark.mean()) - periodic_risk_free
    )
    return periodic_alpha * periods


def rolling_beta(
    asset_returns: pd.Series,
    benchmark_returns: pd.Series,
    window: int = 60,
) -> pd.Series:
    """Calculate backward-looking rolling beta on aligned observations."""

    window_size = _positive_integer(window, name="window", minimum=2)
    asset, benchmark = _aligned_returns(asset_returns, benchmark_returns, minimum_observations=1)
    covariance = asset.rolling(window_size, min_periods=window_size).cov(benchmark, ddof=1)
    variance = benchmark.rolling(window_size, min_periods=window_size).var(ddof=1)
    result = covariance / variance
    return result.where(variance != 0.0)


def rolling_sharpe(
    returns: pd.Series,
    window: int = 60,
    risk_free_rate: float = 0.0,
    periods_per_year: int = 252,
) -> pd.Series:
    """Calculate backward-looking rolling annualized Sharpe using sample std."""

    window_size = _positive_integer(window, name="window", minimum=2)
    periods = _positive_integer(periods_per_year, name="periods_per_year")
    values = _validated_series(returns, name="returns")
    periodic_risk_free = _periodic_risk_free_rate(risk_free_rate, periods)
    excess = values - periodic_risk_free
    rolling_mean = excess.rolling(window_size, min_periods=window_size).mean()
    rolling_std = values.rolling(window_size, min_periods=window_size).std(ddof=1)
    result = rolling_mean / rolling_std * math.sqrt(periods)
    return result.where(rolling_std != 0.0)


@dataclass(frozen=True, slots=True)
class DrawdownPeriod:
    peak_timestamp: object
    trough_timestamp: object
    recovery_timestamp: object | None
    max_drawdown: float
    duration: int


def drawdown_periods(equity: pd.Series) -> list[DrawdownPeriod]:
    """Identify peak-to-recovery periods; recovery is equity >= prior peak.

    Duration counts observations strictly below the prior peak and excludes the
    recovery observation. An unrecovered period ends at the final observation.
    """

    values = _validated_series(equity, name="equity")
    if (values <= 0.0).any():
        raise ValueError("drawdown analysis requires positive equity")

    periods: list[DrawdownPeriod] = []
    peak_value = float(values.iloc[0])
    peak_timestamp = values.index[0]
    in_drawdown = False
    trough_value = peak_value
    trough_timestamp = peak_timestamp
    underwater_observations = 0

    for timestamp, raw_value in values.iloc[1:].items():
        value = float(raw_value)
        if not in_drawdown:
            if value >= peak_value:
                peak_value = value
                peak_timestamp = timestamp
                continue
            in_drawdown = True
            trough_value = value
            trough_timestamp = timestamp
            underwater_observations = 1
            continue

        if value >= peak_value:
            periods.append(
                DrawdownPeriod(
                    peak_timestamp=peak_timestamp,
                    trough_timestamp=trough_timestamp,
                    recovery_timestamp=timestamp,
                    max_drawdown=trough_value / peak_value - 1.0,
                    duration=underwater_observations,
                )
            )
            in_drawdown = False
            peak_value = value
            peak_timestamp = timestamp
            continue

        underwater_observations += 1
        if value < trough_value:
            trough_value = value
            trough_timestamp = timestamp

    if in_drawdown:
        periods.append(
            DrawdownPeriod(
                peak_timestamp=peak_timestamp,
                trough_timestamp=trough_timestamp,
                recovery_timestamp=None,
                max_drawdown=trough_value / peak_value - 1.0,
                duration=underwater_observations,
            )
        )
    return periods


def max_drawdown_period(equity: pd.Series) -> DrawdownPeriod | None:
    """Return the deepest drawdown period, or ``None`` when never underwater."""

    periods = drawdown_periods(equity)
    return min(periods, key=lambda period: period.max_drawdown, default=None)

