from datetime import datetime, timedelta

import pytest

from quant_system.backtest import MarketEvent, SignalEvent, SignalSide
from quant_system.strategy import MovingAverageCrossoverStrategy, PositionState

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
    strategy: MovingAverageCrossoverStrategy,
    prices: list[float],
    symbol: str = "AAPL",
) -> list[list[SignalEvent]]:
    return [strategy.on_market(event(price, index, symbol)) for index, price in enumerate(prices)]


def test_moving_average_warmup_requires_slow_window() -> None:
    strategy = MovingAverageCrossoverStrategy(fast_window=2, slow_window=3)

    assert feed(strategy, [10.0, 10.0]) == [[], []]


def test_bullish_crossover_generates_one_buy() -> None:
    strategy = MovingAverageCrossoverStrategy(fast_window=2, slow_window=3)
    prices = [10.0, 10.0, 10.0, 9.0, 8.0, 9.0, 10.0]

    results = feed(strategy, prices)

    assert results[-1][0].side is SignalSide.BUY
    diagnostic = strategy.diagnostic_for("AAPL")
    assert diagnostic is not None
    # Final window [8, 9, 10]: fast=(9+10)/2=9.5, slow=9.
    assert diagnostic.fast_average == pytest.approx(9.5)
    assert diagnostic.slow_average == pytest.approx(9.0)


def test_bearish_crossover_exits_long() -> None:
    strategy = MovingAverageCrossoverStrategy(fast_window=2, slow_window=3)
    prices = [10.0, 10.0, 10.0, 9.0, 8.0, 9.0, 10.0, 11.0, 8.0]

    results = feed(strategy, prices)

    assert results[6][0].side is SignalSide.BUY
    assert results[8][0].side is SignalSide.EXIT
    assert strategy.state_for("AAPL") is PositionState.FLAT


def test_moving_average_does_not_repeat_buy_above_slow_average() -> None:
    strategy = MovingAverageCrossoverStrategy(fast_window=2, slow_window=3)

    results = feed(strategy, [10.0, 10.0, 10.0, 9.0, 8.0, 9.0, 10.0, 11.0, 12.0])

    assert [result[0].side for result in results if result] == [SignalSide.BUY]


def test_long_only_mode_does_not_short_on_bearish_crossover() -> None:
    strategy = MovingAverageCrossoverStrategy(fast_window=2, slow_window=3)

    results = feed(strategy, [10.0, 10.0, 10.0, 9.0])

    assert results[-1] == []
    assert strategy.state_for("AAPL") is PositionState.FLAT


def test_short_enabled_mode_sells_on_bearish_crossover() -> None:
    strategy = MovingAverageCrossoverStrategy(
        fast_window=2,
        slow_window=3,
        allow_short=True,
    )

    results = feed(strategy, [10.0, 10.0, 10.0, 9.0])

    assert results[-1][0].side is SignalSide.SELL
    assert strategy.state_for("AAPL") is PositionState.SHORT


def test_moving_average_state_is_isolated_per_symbol() -> None:
    strategy = MovingAverageCrossoverStrategy(fast_window=2, slow_window=3)
    aapl_prices = [10.0, 10.0, 10.0, 9.0, 8.0, 9.0, 10.0]
    for index, aapl in enumerate(aapl_prices):
        aapl_signals = strategy.on_market(event(aapl, index, "AAPL"))
        msft_signals = strategy.on_market(event(20.0, index, "MSFT"))

    assert aapl_signals[0].side is SignalSide.BUY
    assert msft_signals == []
    assert strategy.state_for("AAPL") is PositionState.LONG
    assert strategy.state_for("MSFT") is PositionState.FLAT


@pytest.mark.parametrize(
    "fast, slow",
    [(0, 3), (2, 0), (3, 3), (4, 3)],
)
def test_moving_average_window_validation(fast: int, slow: int) -> None:
    with pytest.raises(ValueError):
        MovingAverageCrossoverStrategy(fast_window=fast, slow_window=slow)

