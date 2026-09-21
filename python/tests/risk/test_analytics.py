import math

import pandas as pd
import pytest

from quant_system.risk import (
    alpha,
    beta,
    drawdown_periods,
    max_drawdown_period,
    rolling_beta,
    rolling_sharpe,
    rolling_volatility,
)


def test_rolling_volatility_uses_sample_std_and_annualizes() -> None:
    returns = pd.Series([0.01, 0.02, 0.03, 0.04])

    result = rolling_volatility(returns, window=3, periods_per_year=4)

    assert result.iloc[:2].isna().all()
    # Sample std of 1%, 2%, 3% is 1%; annualization sqrt(4) doubles it.
    assert result.iloc[2] == pytest.approx(0.02)


@pytest.mark.parametrize("window", [0, 1])
def test_rolling_volatility_rejects_short_windows(window: int) -> None:
    with pytest.raises(ValueError, match="window"):
        rolling_volatility(pd.Series([0.01, 0.02]), window=window)


@pytest.mark.parametrize(
    "returns",
    [pd.Series([0.01, float("nan")]), pd.Series([0.01, float("inf")])],
)
def test_rolling_analytics_reject_non_finite_returns(returns: pd.Series) -> None:
    with pytest.raises(ValueError):
        rolling_volatility(returns, window=2)


def test_beta_is_two_for_exact_double_benchmark() -> None:
    benchmark = pd.Series([0.01, 0.02, -0.01, 0.03])
    asset = pd.Series([0.02, 0.04, -0.02, 0.06])

    assert beta(asset, benchmark) == pytest.approx(2.0)


def test_beta_aligns_by_timestamp_not_position() -> None:
    dates = pd.date_range("2026-01-01", periods=4, freq="D")
    benchmark = pd.Series([0.01, 0.02, -0.01, 0.03], index=dates)
    asset = pd.Series(
        [0.06, 0.02, -0.02, 0.04],
        index=[dates[3], dates[0], dates[2], dates[1]],
    )

    assert beta(asset, benchmark) == pytest.approx(2.0)


def test_beta_rejects_zero_benchmark_variance() -> None:
    with pytest.raises(ValueError, match="variance"):
        beta(pd.Series([0.01, 0.02]), pd.Series([0.01, 0.01]))


def test_alpha_is_arithmetic_annualized_capm_intercept() -> None:
    benchmark = pd.Series([0.01, 0.02, -0.01, 0.03])
    asset = 0.001 + 1.5 * benchmark

    assert alpha(asset, benchmark, periods_per_year=252) == pytest.approx(0.252)


def test_rolling_beta_is_backward_looking() -> None:
    benchmark = pd.Series([0.01, 0.02, -0.01, 0.03])
    asset = 2.0 * benchmark

    result = rolling_beta(asset, benchmark, window=3)

    assert result.iloc[:2].isna().all()
    assert result.iloc[2:].tolist() == pytest.approx([2.0, 2.0])


def test_rolling_sharpe_uses_only_current_and_prior_window() -> None:
    returns = pd.Series([0.01, 0.02, 0.03, -0.01])

    result = rolling_sharpe(returns, window=3, periods_per_year=4)

    assert result.iloc[:2].isna().all()
    assert result.iloc[2] == pytest.approx(4.0)
    expected_last = ((0.02 + 0.03 - 0.01) / 3) / pd.Series([0.02, 0.03, -0.01]).std()
    assert result.iloc[3] == pytest.approx(expected_last * math.sqrt(4))


def test_drawdown_period_identifies_peak_trough_recovery_and_duration() -> None:
    dates = pd.date_range("2026-01-01", periods=7, freq="D")
    equity = pd.Series([100.0, 120.0, 110.0, 90.0, 100.0, 120.0, 130.0], index=dates)

    period = max_drawdown_period(equity)

    assert period is not None
    assert period.peak_timestamp == dates[1]
    assert period.trough_timestamp == dates[3]
    assert period.recovery_timestamp == dates[5]
    assert period.max_drawdown == pytest.approx(-0.25)
    assert period.duration == 3


def test_unrecovered_drawdown_has_no_recovery_timestamp() -> None:
    equity = pd.Series([100.0, 120.0, 110.0, 90.0])

    periods = drawdown_periods(equity)

    assert len(periods) == 1
    assert periods[0].recovery_timestamp is None
    assert periods[0].duration == 2


def test_no_drawdown_returns_no_period() -> None:
    assert max_drawdown_period(pd.Series([100.0, 110.0, 120.0])) is None
