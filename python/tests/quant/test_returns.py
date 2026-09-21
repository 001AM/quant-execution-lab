import math

import numpy as np
import pandas as pd
import pytest

from quant_system.quant import cumulative_returns, equity_curve, log_returns, simple_returns


def test_simple_returns_preserve_initial_nan_and_index() -> None:
    index = pd.date_range("2026-01-01", periods=3, freq="D")
    prices = pd.Series([100.0, 105.0, 102.0], index=index, name="AAPL")

    result = simple_returns(prices)

    assert result.index.equals(index)
    assert result.name == "AAPL"
    assert math.isnan(result.iloc[0])
    assert result.iloc[1] == pytest.approx(0.05)
    assert result.iloc[2] == pytest.approx(-0.02857142857142857)


def test_simple_returns_empty_series_is_empty() -> None:
    result = simple_returns(pd.Series([], dtype=float))

    assert result.empty
    assert result.dtype == float


def test_simple_returns_reject_zero_prior_price() -> None:
    with pytest.raises(ValueError, match="prior price is zero"):
        simple_returns(pd.Series([0.0, 100.0]))


@pytest.mark.parametrize(
    "prices, message",
    [
        (pd.Series([100.0, np.nan]), "missing"),
        (pd.Series([100.0, np.inf]), "finite"),
        (pd.Series(["100", "101"]), "numeric"),
    ],
)
def test_simple_returns_reject_invalid_prices(prices: pd.Series, message: str) -> None:
    with pytest.raises(ValueError, match=message):
        simple_returns(prices)


def test_log_returns_use_natural_logarithm() -> None:
    prices = pd.Series([100.0, 105.0, 102.0])

    result = log_returns(prices)

    assert math.isnan(result.iloc[0])
    assert result.iloc[1] == pytest.approx(math.log(1.05))
    assert result.iloc[2] == pytest.approx(math.log(102.0 / 105.0))


@pytest.mark.parametrize("bad_price", [0.0, -1.0])
def test_log_returns_require_strictly_positive_prices(bad_price: float) -> None:
    prices = pd.Series([100.0, bad_price])

    with pytest.raises(ValueError, match="strictly positive"):
        log_returns(prices)


def test_cumulative_returns_compound_instead_of_sum() -> None:
    returns = pd.Series([0.10, -0.05, 0.02])

    result = cumulative_returns(returns)

    assert result.tolist() == pytest.approx([0.10, 0.045, 0.0659])
    assert result.iloc[-1] == pytest.approx(1.10 * 0.95 * 1.02 - 1.0)


def test_cumulative_returns_preserve_single_leading_nan() -> None:
    returns = pd.Series([np.nan, 0.10, -0.05])

    result = cumulative_returns(returns)

    assert math.isnan(result.iloc[0])
    assert result.iloc[1:].tolist() == pytest.approx([0.10, 0.045])


@pytest.mark.parametrize(
    "returns, message",
    [
        (pd.Series([0.1, np.nan]), "only one leading NaN"),
        (pd.Series([0.1, np.inf]), "finite"),
        (pd.Series([-1.01]), "below -1.0"),
    ],
)
def test_cumulative_returns_reject_invalid_returns(
    returns: pd.Series, message: str
) -> None:
    with pytest.raises(ValueError, match=message):
        cumulative_returns(returns)


def test_equity_curve_compounds_from_initial_capital() -> None:
    returns = pd.Series([0.10, -0.05, 0.02], index=["a", "b", "c"])

    result = equity_curve(returns, initial_capital=100_000.0)

    assert result.index.tolist() == ["a", "b", "c"]
    assert result.tolist() == pytest.approx([110_000.0, 104_500.0, 106_590.0])


def test_equity_curve_preserves_leading_nan() -> None:
    result = equity_curve(pd.Series([np.nan, 0.10]), initial_capital=1_000.0)

    assert math.isnan(result.iloc[0])
    assert result.iloc[1] == pytest.approx(1_100.0)


@pytest.mark.parametrize("capital", [0.0, -100.0, np.nan, np.inf])
def test_equity_curve_rejects_invalid_initial_capital(capital: float) -> None:
    with pytest.raises(ValueError, match="initial_capital"):
        equity_curve(pd.Series([0.01]), initial_capital=capital)
