"""Performance metrics for validated simple-return and equity series."""

import math
from dataclasses import dataclass

import numpy as np
import pandas as pd

from quant_system.quant.returns import equity_curve


def _positive_periods_per_year(periods_per_year: int) -> int:
    if isinstance(periods_per_year, bool) or not isinstance(periods_per_year, int):
        raise TypeError("periods_per_year must be an integer")
    if periods_per_year <= 0:
        raise ValueError("periods_per_year must be greater than zero")
    return periods_per_year


def _numeric_series(values: pd.Series, *, label: str, allow_empty: bool = False) -> pd.Series:
    if not isinstance(values, pd.Series):
        raise TypeError(f"{label} must be a pandas Series")
    if values.empty:
        if allow_empty:
            return values.astype(float).copy()
        raise ValueError(f"{label} must not be empty")
    if pd.api.types.is_bool_dtype(values.dtype) or not pd.api.types.is_numeric_dtype(values.dtype):
        raise ValueError(f"{label} must contain numeric values")

    result = values.astype(float).copy()
    if result.isna().any():
        raise ValueError(f"{label} must not contain missing values")
    if not np.isfinite(result.to_numpy(dtype=float)).all():
        raise ValueError(f"{label} must contain only finite values")
    return result


def _clean_returns(returns: pd.Series, *, minimum_observations: int = 1) -> pd.Series:
    if not isinstance(returns, pd.Series):
        raise TypeError("returns must be a pandas Series")
    if pd.api.types.is_bool_dtype(returns.dtype) or not pd.api.types.is_numeric_dtype(
        returns.dtype
    ):
        raise ValueError("returns must contain numeric values")

    values = returns.astype(float).copy()
    missing = values.isna()
    if missing.any():
        leading_nan_only = (
            len(values) > 0 and bool(missing.iloc[0]) and int(missing.sum()) == 1
        )
        if not leading_nan_only:
            raise ValueError("returns may contain only one leading NaN")
        values = values.iloc[1:]

    if len(values) < minimum_observations:
        raise ValueError(f"returns requires at least {minimum_observations} observations")
    if not np.isfinite(values.to_numpy(dtype=float)).all():
        raise ValueError("returns must contain only finite values")
    below_total_loss = values < -1.0
    if below_total_loss.any():
        bad_value = values[below_total_loss].iloc[0]
        raise ValueError(f"simple return {bad_value} is below -1.0 (a loss greater than 100%)")
    return values


def _periodic_risk_free_rate(risk_free_rate: float, periods_per_year: int) -> float:
    if isinstance(risk_free_rate, bool) or not isinstance(risk_free_rate, int | float):
        raise TypeError("risk_free_rate must be a real number")
    annual_rate = float(risk_free_rate)
    if not math.isfinite(annual_rate) or annual_rate <= -1.0:
        raise ValueError("risk_free_rate must be finite and greater than -1.0")
    return float((1.0 + annual_rate) ** (1.0 / periods_per_year) - 1.0)


def cagr(equity: pd.Series, periods_per_year: int = 252) -> float:
    """Calculate annual compound growth from an equity series.

    With ``n`` equity observations there are ``n - 1`` compounding intervals,
    so elapsed years are ``(n - 1) / periods_per_year``.
    """

    periods = _positive_periods_per_year(periods_per_year)
    values = _numeric_series(equity, label="equity")
    if len(values) < 2:
        raise ValueError("equity requires at least two observations to calculate CAGR")
    if (values <= 0.0).any():
        raise ValueError("CAGR requires all equity values to be greater than zero")

    elapsed_periods = len(values) - 1
    growth = float(values.iloc[-1] / values.iloc[0])
    return float(growth ** (periods / elapsed_periods) - 1.0)


def annualized_volatility(returns: pd.Series, periods_per_year: int = 252) -> float:
    """Calculate annualized volatility using sample standard deviation (ddof=1)."""

    periods = _positive_periods_per_year(periods_per_year)
    values = _clean_returns(returns, minimum_observations=2)
    periodic_volatility = float(values.std(ddof=1))
    return periodic_volatility * math.sqrt(periods)


def sharpe_ratio(
    returns: pd.Series,
    risk_free_rate: float = 0.0,
    periods_per_year: int = 252,
) -> float:
    """Calculate the annualized Sharpe ratio.

    ``risk_free_rate`` is an effective annual rate and is converted to an
    effective periodic rate by geometric compounding. Volatility uses sample
    standard deviation.
    """

    periods = _positive_periods_per_year(periods_per_year)
    values = _clean_returns(returns, minimum_observations=2)
    periodic_risk_free = _periodic_risk_free_rate(risk_free_rate, periods)
    volatility = float(values.std(ddof=1))
    if volatility == 0.0:
        raise ValueError("Sharpe ratio is undefined when return volatility is zero")
    mean_excess_return = float((values - periodic_risk_free).mean())
    return mean_excess_return / volatility * math.sqrt(periods)


def sortino_ratio(
    returns: pd.Series,
    risk_free_rate: float = 0.0,
    periods_per_year: int = 252,
) -> float:
    """Calculate the annualized Sortino ratio.

    Downside deviation is the target semideviation
    ``sqrt(mean(min(excess_return, 0) ** 2))`` across all observations. The
    target is the periodic risk-free rate converted from its annual rate.
    """

    periods = _positive_periods_per_year(periods_per_year)
    values = _clean_returns(returns)
    periodic_risk_free = _periodic_risk_free_rate(risk_free_rate, periods)
    excess_returns = values - periodic_risk_free
    if not (excess_returns < 0.0).any():
        raise ValueError("Sortino ratio is undefined without downside observations")

    downside = np.minimum(excess_returns.to_numpy(dtype=float), 0.0)
    downside_deviation = float(np.sqrt(np.mean(np.square(downside))))
    if downside_deviation == 0.0:
        raise ValueError("Sortino ratio is undefined when downside deviation is zero")
    return float(excess_returns.mean()) / downside_deviation * math.sqrt(periods)


def drawdown(equity: pd.Series) -> pd.Series:
    """Return the negative percentage decline from each running equity peak."""

    values = _numeric_series(equity, label="equity", allow_empty=True)
    if values.empty:
        return values
    if values.iloc[0] <= 0.0:
        raise ValueError("drawdown requires starting equity to be greater than zero")
    if (values < 0.0).any():
        raise ValueError("drawdown does not support negative equity values")
    return values / values.cummax() - 1.0


def max_drawdown(equity: pd.Series) -> float:
    """Return the worst drawdown as a non-positive number."""

    drawdowns = drawdown(equity)
    if drawdowns.empty:
        raise ValueError("equity must not be empty")
    return float(drawdowns.min())


@dataclass(frozen=True, slots=True)
class PerformanceReport:
    """A typed summary of compounded simple-return performance."""

    total_return: float
    cagr: float
    annualized_volatility: float
    sharpe_ratio: float
    sortino_ratio: float
    max_drawdown: float
    initial_capital: float
    ending_equity: float
    observations: int

    def to_dict(self) -> dict[str, float | int]:
        """Return a serialization-friendly representation."""

        return {
            "total_return": self.total_return,
            "cagr": self.cagr,
            "annualized_volatility": self.annualized_volatility,
            "sharpe_ratio": self.sharpe_ratio,
            "sortino_ratio": self.sortino_ratio,
            "max_drawdown": self.max_drawdown,
            "initial_capital": self.initial_capital,
            "ending_equity": self.ending_equity,
            "observations": self.observations,
        }


def performance_report(
    returns: pd.Series,
    risk_free_rate: float = 0.0,
    periods_per_year: int = 252,
    initial_capital: float = 100_000.0,
) -> PerformanceReport:
    """Calculate the requested performance metrics in one typed report."""

    periods = _positive_periods_per_year(periods_per_year)
    values = _clean_returns(returns, minimum_observations=2)
    curve = equity_curve(values, initial_capital=initial_capital)
    starting_equity = float(initial_capital)
    equity_with_baseline = pd.concat(
        [pd.Series([starting_equity], dtype=float), curve.reset_index(drop=True)],
        ignore_index=True,
    )
    ending_equity = float(curve.iloc[-1])

    return PerformanceReport(
        total_return=ending_equity / starting_equity - 1.0,
        cagr=cagr(equity_with_baseline, periods_per_year=periods),
        annualized_volatility=annualized_volatility(values, periods_per_year=periods),
        sharpe_ratio=sharpe_ratio(
            values,
            risk_free_rate=risk_free_rate,
            periods_per_year=periods,
        ),
        sortino_ratio=sortino_ratio(
            values,
            risk_free_rate=risk_free_rate,
            periods_per_year=periods,
        ),
        max_drawdown=max_drawdown(equity_with_baseline),
        initial_capital=starting_equity,
        ending_equity=ending_equity,
        observations=len(values),
    )


def performance_summary(
    returns: pd.Series,
    risk_free_rate: float = 0.0,
    periods_per_year: int = 252,
    initial_capital: float = 100_000.0,
) -> dict[str, float | int]:
    """Return :func:`performance_report` as a plain dictionary."""

    return performance_report(
        returns,
        risk_free_rate=risk_free_rate,
        periods_per_year=periods_per_year,
        initial_capital=initial_capital,
    ).to_dict()
