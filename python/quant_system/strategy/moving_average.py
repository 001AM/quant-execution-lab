"""Incremental moving-average crossover strategy."""

import statistics
from collections import deque
from dataclasses import dataclass
from datetime import datetime

from quant_system.backtest.events import MarketEvent, SignalEvent, SignalSide
from quant_system.strategy.base import PositionState, Strategy


@dataclass(frozen=True, slots=True)
class MovingAverageDiagnostic:
    timestamp: datetime
    fast_average: float
    slow_average: float


class MovingAverageCrossoverStrategy(Strategy):
    """Trade actual fast/slow average crossovers, long-only by default."""

    def __init__(
        self,
        fast_window: int,
        slow_window: int,
        *,
        allow_short: bool = False,
    ) -> None:
        for name, value in (("fast_window", fast_window), ("slow_window", slow_window)):
            if isinstance(value, bool) or not isinstance(value, int):
                raise TypeError(f"{name} must be an integer")
            if value <= 0:
                raise ValueError(f"{name} must be positive")
        if fast_window >= slow_window:
            raise ValueError("fast_window must be lower than slow_window")
        if not isinstance(allow_short, bool):
            raise TypeError("allow_short must be a bool")

        self.fast_window = fast_window
        self.slow_window = slow_window
        self.allow_short = allow_short
        self._history: dict[str, deque[float]] = {}
        self._state: dict[str, PositionState] = {}
        self._previous_difference: dict[str, float] = {}
        self._diagnostics: dict[str, MovingAverageDiagnostic] = {}

    def reset(self) -> None:
        self._history.clear()
        self._state.clear()
        self._previous_difference.clear()
        self._diagnostics.clear()

    def state_for(self, symbol: str) -> PositionState:
        return self._state.get(symbol, PositionState.FLAT)

    def diagnostic_for(self, symbol: str) -> MovingAverageDiagnostic | None:
        return self._diagnostics.get(symbol)

    def on_market(self, event: MarketEvent) -> list[SignalEvent]:
        return self._observe(event, emit_signals=True)

    def warm_up(self, event: MarketEvent) -> None:
        self._observe(event, emit_signals=False)

    def _observe(self, event: MarketEvent, *, emit_signals: bool) -> list[SignalEvent]:
        history = self._history.setdefault(event.symbol, deque(maxlen=self.slow_window))
        history.append(event.close)
        if len(history) < self.slow_window:
            return []

        prices = tuple(history)
        fast_average = statistics.fmean(prices[-self.fast_window :])
        slow_average = statistics.fmean(prices)
        difference = fast_average - slow_average
        self._diagnostics[event.symbol] = MovingAverageDiagnostic(
            event.timestamp,
            fast_average,
            slow_average,
        )

        previous = self._previous_difference.get(event.symbol)
        self._previous_difference[event.symbol] = difference
        if previous is None or not emit_signals:
            return []

        bullish_crossover = previous <= 0.0 and difference > 0.0
        bearish_crossover = previous >= 0.0 and difference < 0.0
        state = self.state_for(event.symbol)

        if bullish_crossover and state is PositionState.FLAT:
            return self._transition(event, SignalSide.BUY, PositionState.LONG)
        if bullish_crossover and state is PositionState.SHORT:
            return self._transition(event, SignalSide.EXIT, PositionState.FLAT)
        if bearish_crossover and state is PositionState.LONG:
            return self._transition(event, SignalSide.EXIT, PositionState.FLAT)
        if bearish_crossover and state is PositionState.FLAT and self.allow_short:
            return self._transition(event, SignalSide.SELL, PositionState.SHORT)
        return []

    def _transition(
        self,
        event: MarketEvent,
        side: SignalSide,
        state: PositionState,
    ) -> list[SignalEvent]:
        self._state[event.symbol] = state
        return [SignalEvent(event.timestamp, event.symbol, side)]
