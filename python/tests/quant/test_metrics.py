import math

import numpy as np
import pandas as pd
import pytest

from quant_system.quant import (
    PerformanceReport,
    annualized_volatility,
    cagr,
    drawdown,
    max_drawdown,
    performance_report,
    performance_summary,
    sharpe_ratio,
    sortino_ratio,
)


def test_cagr_uses_equity_intervals() -> None:
    equity = pd.Series([100.0, 110.0, 121.0])

    result = cagr(equity, periods_per_year=2)

    assert result == pytest.approx(0.21)


@pytest.mark.parametrize(
    "equity, message",
    [
        (pd.Series([], dtype=float), "must not be empty"),
        (pd.Series([100.0]), "at least two"),
        (pd.Series([0.0, 100.0]), "greater than zero"),
        (pd.Series([100.0, -1.0]), "greater than zero"),
        (pd.Series([100.0, np.nan]), "missing"),
        (pd.Series([100.0, np.inf]), "finite"),
    ],
)
def test_cagr_rejects_invalid_equity(equity: pd.Series, message: str) -> None:
    with pytest.raises(ValueError, match=message):
        cagr(equity)


@pytest.mark.parametrize("periods", [0, -1])
def test_metrics_require_positive_periods_per_year(periods: int) -> None:
    with pytest.raises(ValueError, match="greater than zero"):
        annualized_volatility(pd.Series([0.01, 0.02]), periods_per_year=periods)


def test_annualized_volatility_uses_sample_standard_deviation() -> None:
    returns = pd.Series([0.01, 0.02, 0.03])

    result = annualized_volatility(returns, periods_per_year=4)

    assert result == pytest.approx(0.02)


def test_annualized_volatility_ignores_only_a_leading_return_nan() -> None:
    returns = pd.Series([np.nan, 0.01, 0.02, 0.03])

    result = annualized_volatility(returns, periods_per_year=4)

    assert result == pytest.approx(0.02)


@pytest.mark.parametrize(
    "returns, message",
    [
        (pd.Series([], dtype=float), "at least 2"),
        (pd.Series([0.01]), "at least 2"),
        (pd.Series([0.01, np.nan]), "only one leading NaN"),
        (pd.Series([0.01, np.inf]), "finite"),
    ],
)
def test_annualized_volatility_rejects_invalid_returns(
    returns: pd.Series, message: str
) -> None:
    with pytest.raises(ValueError, match=message):
        annualized_volatility(returns)


def test_sharpe_geometrically_converts_annual_risk_free_rate() -> None:
    # A 21% effective annual rate is 10% per period when there are two periods.
    returns = pd.Series([0.10, 0.20])

    result = sharpe_ratio(returns, risk_free_rate=0.21, periods_per_year=2)

    assert result == pytest.approx(1.0)


def test_sharpe_rejects_zero_volatility() -> None:
    with pytest.raises(ValueError, match="volatility is zero"):
        sharpe_ratio(pd.Series([0.01, 0.01, 0.01]))


@pytest.mark.parametrize("risk_free_rate", [-1.0, np.nan, np.inf])
def test_sharpe_rejects_invalid_risk_free_rate(risk_free_rate: float) -> None:
    with pytest.raises(ValueError, match="risk_free_rate"):
        sharpe_ratio(pd.Series([0.01, 0.02]), risk_free_rate=risk_free_rate)


def test_sortino_uses_target_semideviation() -> None:
    returns = pd.Series([0.10, -0.05, 0.02])
    expected_downside_deviation = math.sqrt((0.05**2) / 3.0)
    expected = returns.mean() / expected_downside_deviation

    result = sortino_ratio(returns, periods_per_year=1)

    assert result == pytest.approx(expected)


def test_sortino_rejects_no_downside_observations() -> None:
    with pytest.raises(ValueError, match="without downside observations"):
        sortino_ratio(pd.Series([0.01, 0.02, 0.03]))


def test_sortino_rejects_zero_downside_deviation() -> None:
    returns = pd.Series([-5e-324, 0.01])

    with pytest.raises(ValueError, match="downside deviation is zero"):
        sortino_ratio(returns, periods_per_year=1)


def test_drawdown_and_max_drawdown_use_negative_sign_convention() -> None:
    equity = pd.Series([100.0, 120.0, 90.0, 110.0, 130.0])

    result = drawdown(equity)

    assert result.tolist() == pytest.approx([0.0, 0.0, -0.25, -1.0 / 12.0, 0.0])
    assert max_drawdown(equity) == pytest.approx(-0.25)


def test_drawdown_preserves_index_and_empty_series() -> None:
    index = pd.date_range("2026-01-01", periods=2, freq="D")
    result = drawdown(pd.Series([100.0, 90.0], index=index))

    assert result.index.equals(index)
    assert drawdown(pd.Series([], dtype=float)).empty


@pytest.mark.parametrize("bad_value", [-1.0, np.nan, np.inf])
def test_drawdown_rejects_invalid_equity(bad_value: float) -> None:
    with pytest.raises(ValueError):
        drawdown(pd.Series([100.0, bad_value]))


def test_drawdown_represents_total_loss_as_negative_one() -> None:
    equity = pd.Series([100.0, 0.0, 0.0])

    assert drawdown(equity).tolist() == pytest.approx([0.0, -1.0, -1.0])
    assert max_drawdown(equity) == pytest.approx(-1.0)


def test_drawdown_rejects_zero_starting_equity() -> None:
    with pytest.raises(ValueError, match="starting equity"):
        drawdown(pd.Series([0.0, 100.0]))


def test_max_drawdown_rejects_empty_equity() -> None:
    with pytest.raises(ValueError, match="must not be empty"):
        max_drawdown(pd.Series([], dtype=float))


def test_performance_report_aggregates_requested_metrics() -> None:
    returns = pd.Series([0.10, -0.05, 0.02])

    report = performance_report(
        returns,
        risk_free_rate=0.0,
        periods_per_year=3,
        initial_capital=100_000.0,
    )

    assert isinstance(report, PerformanceReport)
    assert report.total_return == pytest.approx(0.0659)
    assert report.cagr == pytest.approx(0.0659)
    assert report.ending_equity == pytest.approx(106_590.0)
    assert report.initial_capital == 100_000.0
    assert report.observations == 3
    assert report.max_drawdown == pytest.approx(-0.05)
    assert report.annualized_volatility == pytest.approx(
        annualized_volatility(returns, periods_per_year=3)
    )
    assert report.sharpe_ratio == pytest.approx(sharpe_ratio(returns, periods_per_year=3))
    assert report.sortino_ratio == pytest.approx(sortino_ratio(returns, periods_per_year=3))


def test_performance_summary_returns_plain_dictionary_and_accepts_leading_nan() -> None:
    returns = pd.Series([np.nan, 0.10, -0.05, 0.02])

    summary = performance_summary(returns, periods_per_year=3)

    assert summary["observations"] == 3
    assert summary["total_return"] == pytest.approx(0.0659)
    assert set(summary) == {
        "total_return",
        "cagr",
        "annualized_volatility",
        "sharpe_ratio",
        "sortino_ratio",
        "max_drawdown",
        "initial_capital",
        "ending_equity",
        "observations",
    }
