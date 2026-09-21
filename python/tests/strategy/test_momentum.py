from datetime import datetime, timedelta

import pytest

from quant_system.backtest import MarketEvent, SignalSide
from quant_system.strategy import MomentumStrategy, PositionState

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
    strategy: MomentumStrategy,
    prices: list[float],
    symbol: str = "AAPL",
) -> list[list[object]]:
    return [strategy.on_market(event(price, index, symbol)) for index, price in enumerate(prices)]


def test_momentum_warmup_requires_lookback_plus_one_prices() -> None:
    strategy = MomentumStrategy(lookback=3, threshold=0.02)

    results = feed(strategy, [100.0, 101.0, 102.0])

    assert results == [[], [], []]
    assert strategy.diagnostic_for("AAPL") is None


def test_positive_momentum_generates_buy_with_current_timestamp() -> None:
    strategy = MomentumStrategy(lookback=3, threshold=0.02)

    results = feed(strategy, [100.0, 100.0, 100.0, 105.0])
    signal = results[-1][0]

    assert signal.side is SignalSide.BUY  # type: ignore[attr-defined]
    assert signal.timestamp == START + timedelta(days=3)  # type: ignore[attr-defined]
    diagnostic = strategy.diagnostic_for("AAPL")
    assert diagnostic is not None
    assert diagnostic.momentum == pytest.approx(0.05)
    assert strategy.state_for("AAPL") is PositionState.LONG


def test_negative_momentum_generates_sell() -> None:
    strategy = MomentumStrategy(lookback=3, threshold=0.02)

    results = feed(strategy, [100.0, 100.0, 100.0, 95.0])

    assert results[-1][0].side is SignalSide.SELL  # type: ignore[attr-defined]
    assert strategy.diagnostic_for("AAPL").momentum == pytest.approx(-0.05)  # type: ignore[union-attr]
    assert strategy.state_for("AAPL") is PositionState.SHORT


def test_momentum_below_threshold_emits_no_signal() -> None:
    strategy = MomentumStrategy(lookback=3, threshold=0.02)

    results = feed(strategy, [100.0, 100.0, 100.0, 101.0])

    assert results[-1] == []
    assert strategy.state_for("AAPL") is PositionState.FLAT


def test_momentum_does_not_repeat_buy_while_long() -> None:
    strategy = MomentumStrategy(lookback=3, threshold=0.02)

    results = feed(strategy, [100.0, 100.0, 100.0, 105.0, 106.0])

    assert [len(result) for result in results] == [0, 0, 0, 1, 0]


def test_opposing_momentum_exits_existing_state() -> None:
    strategy = MomentumStrategy(lookback=2, threshold=0.02)

    results = feed(strategy, [100.0, 100.0, 105.0, 100.0, 95.0])

    assert results[2][0].side is SignalSide.BUY  # type: ignore[attr-defined]
    assert results[4][0].side is SignalSide.EXIT  # type: ignore[attr-defined]
    assert strategy.state_for("AAPL") is PositionState.FLAT


def test_momentum_state_is_isolated_per_symbol() -> None:
    strategy = MomentumStrategy(lookback=2, threshold=0.02)
    for index, price in enumerate([100.0, 100.0, 105.0]):
        aapl_signals = strategy.on_market(event(price, index, "AAPL"))
        msft_signals = strategy.on_market(event(200.0, index, "MSFT"))

    assert aapl_signals[0].side is SignalSide.BUY
    assert msft_signals == []
    assert strategy.state_for("AAPL") is PositionState.LONG
    assert strategy.state_for("MSFT") is PositionState.FLAT


@pytest.mark.parametrize("lookback", [0, -1])
def test_invalid_momentum_lookback(lookback: int) -> None:
    with pytest.raises(ValueError, match="lookback"):
        MomentumStrategy(lookback=lookback, threshold=0.02)


@pytest.mark.parametrize("threshold", [-0.01, float("nan"), float("inf")])
def test_invalid_momentum_threshold(threshold: float) -> None:
    with pytest.raises(ValueError, match="threshold"):
        MomentumStrategy(lookback=3, threshold=threshold)

