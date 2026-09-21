from datetime import datetime, timedelta

import pytest

from quant_system.backtest import MarketEvent, SignalEvent, SignalSide
from quant_system.strategy import MeanReversionStrategy, PositionState

START = datetime(2026, 1, 1)


def event(price: float, offset: int, symbol: str = "AAPL") -> MarketEvent:
    return MarketEvent(
        START + timedelta(days=offset),
        symbol,
        price,
        price,
        price,
        price,
        1_000.0,
    )


def feed(
    strategy: MeanReversionStrategy,
    prices: list[float],
    symbol: str = "AAPL",
) -> list[list[SignalEvent]]:
    return [strategy.on_market(event(price, index, symbol)) for index, price in enumerate(prices)]


def test_mean_reversion_warmup() -> None:
    strategy = MeanReversionStrategy(window=5, entry_z=1.5, exit_z=0.5)

    assert feed(strategy, [100.0] * 4) == [[], [], [], []]


def test_negative_z_score_generates_buy_with_population_std() -> None:
    strategy = MeanReversionStrategy(window=5, entry_z=1.5, exit_z=0.5)

    results = feed(strategy, [100.0, 100.0, 100.0, 100.0, 90.0])

    assert results[-1][0].side is SignalSide.BUY
    diagnostic = strategy.diagnostic_for("AAPL")
    assert diagnostic is not None
    # Hand calculation: mean=98, population variance=16, std=4, z=(90-98)/4=-2.
    assert diagnostic.rolling_mean == pytest.approx(98.0)
    assert diagnostic.rolling_std == pytest.approx(4.0)
    assert diagnostic.z_score == pytest.approx(-2.0)


def test_positive_z_score_generates_sell() -> None:
    strategy = MeanReversionStrategy(window=5, entry_z=1.5, exit_z=0.5)

    results = feed(strategy, [100.0, 100.0, 100.0, 100.0, 110.0])

    assert results[-1][0].side is SignalSide.SELL
    assert strategy.diagnostic_for("AAPL").z_score == pytest.approx(2.0)  # type: ignore[union-attr]


def test_long_exits_when_z_score_returns_toward_zero() -> None:
    strategy = MeanReversionStrategy(window=5, entry_z=1.5, exit_z=0.5)

    results = feed(strategy, [100.0, 100.0, 100.0, 100.0, 90.0, 100.0])

    assert results[4][0].side is SignalSide.BUY
    assert results[5][0].side is SignalSide.EXIT
    assert strategy.state_for("AAPL") is PositionState.FLAT


def test_short_exits_when_z_score_returns_toward_zero() -> None:
    strategy = MeanReversionStrategy(window=5, entry_z=1.5, exit_z=0.5)

    results = feed(strategy, [100.0, 100.0, 100.0, 100.0, 110.0, 100.0])

    assert results[4][0].side is SignalSide.SELL
    assert results[5][0].side is SignalSide.EXIT


def test_zero_standard_deviation_records_zero_and_emits_nothing() -> None:
    strategy = MeanReversionStrategy(window=5, entry_z=1.5, exit_z=0.5)

    results = feed(strategy, [100.0] * 5)

    assert results[-1] == []
    diagnostic = strategy.diagnostic_for("AAPL")
    assert diagnostic is not None
    assert diagnostic.rolling_std == 0.0
    assert diagnostic.z_score == 0.0


def test_mean_reversion_does_not_repeat_entry() -> None:
    strategy = MeanReversionStrategy(window=5, entry_z=1.0, exit_z=0.2)

    results = feed(strategy, [100.0, 100.0, 100.0, 100.0, 90.0, 89.0])

    assert results[4][0].side is SignalSide.BUY
    assert results[5] == []


def test_mean_reversion_state_is_isolated_per_symbol() -> None:
    strategy = MeanReversionStrategy(window=3, entry_z=1.0, exit_z=0.2)
    for index, (aapl, msft) in enumerate(zip([100.0, 100.0, 90.0], [200.0] * 3, strict=True)):
        aapl_signals = strategy.on_market(event(aapl, index, "AAPL"))
        msft_signals = strategy.on_market(event(msft, index, "MSFT"))

    assert aapl_signals[0].side is SignalSide.BUY
    assert msft_signals == []
    assert strategy.state_for("AAPL") is PositionState.LONG
    assert strategy.state_for("MSFT") is PositionState.FLAT


@pytest.mark.parametrize("window", [0, 1, -1])
def test_invalid_mean_reversion_window(window: int) -> None:
    with pytest.raises(ValueError, match="window"):
        MeanReversionStrategy(window=window, entry_z=2.0, exit_z=0.5)


@pytest.mark.parametrize(
    "entry_z, exit_z",
    [(0.0, 0.0), (-1.0, 0.5), (2.0, -0.1), (2.0, 2.0), (2.0, 3.0)],
)
def test_invalid_mean_reversion_thresholds(entry_z: float, exit_z: float) -> None:
    with pytest.raises(ValueError):
        MeanReversionStrategy(window=5, entry_z=entry_z, exit_z=exit_z)

