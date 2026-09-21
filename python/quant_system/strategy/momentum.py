"""Incremental price-momentum strategy."""

import math
from collections import deque
from dataclasses import dataclass
from datetime import datetime

from quant_system.backtest.events import MarketEvent, SignalEvent, SignalSide
from quant_system.strategy.base import PositionState, Strategy


@dataclass(frozen=True, slots=True)
class MomentumDiagnostic:
    timestamp: datetime
    momentum: float


class MomentumStrategy(Strategy):
    """Trade threshold crossings in lookback simple price momentum.

    FLAT emits BUY above ``threshold`` or SELL below ``-threshold``. An
    opposing threshold while LONG/SHORT emits EXIT. The strategy changes its
    intended state when emitting the signal, preventing repeated entries.
    """

    def __init__(self, lookback: int, threshold: float) -> None:
        if isinstance(lookback, bool) or not isinstance(lookback, int):
            raise TypeError("lookback must be an integer")
        if lookback <= 0:
            raise ValueError("lookback must be positive")
        if isinstance(threshold, bool) or not isinstance(threshold, int | float):
            raise TypeError("threshold must be a real number")
        if not math.isfinite(float(threshold)) or threshold < 0.0:
            raise ValueError("threshold must be finite and non-negative")

        self.lookback = lookback
        self.threshold = float(threshold)
        self._history: dict[str, deque[float]] = {}
        self._state: dict[str, PositionState] = {}
        self._diagnostics: dict[str, MomentumDiagnostic] = {}

    def reset(self) -> None:
        self._history.clear()
        self._state.clear()
        self._diagnostics.clear()

    def state_for(self, symbol: str) -> PositionState:
        return self._state.get(symbol, PositionState.FLAT)

    def diagnostic_for(self, symbol: str) -> MomentumDiagnostic | None:
        return self._diagnostics.get(symbol)

    def on_market(self, event: MarketEvent) -> list[SignalEvent]:
        return self._observe(event, emit_signals=True)

    def warm_up(self, event: MarketEvent) -> None:
        self._observe(event, emit_signals=False)

    def _observe(self, event: MarketEvent, *, emit_signals: bool) -> list[SignalEvent]:
        history = self._history.setdefault(
            event.symbol,
            deque(maxlen=self.lookback + 1),
        )
        history.append(event.close)
        if len(history) < self.lookback + 1:
            return []

        momentum = event.close / history[0] - 1.0
        self._diagnostics[event.symbol] = MomentumDiagnostic(event.timestamp, momentum)
        if not emit_signals:
            return []
        state = self.state_for(event.symbol)

        if state is PositionState.FLAT and momentum > self.threshold:
            return self._transition(event, SignalSide.BUY, PositionState.LONG)
        if state is PositionState.FLAT and momentum < -self.threshold:
            return self._transition(event, SignalSide.SELL, PositionState.SHORT)
        if state is PositionState.LONG and momentum < -self.threshold:
            return self._transition(event, SignalSide.EXIT, PositionState.FLAT)
        if state is PositionState.SHORT and momentum > self.threshold:
            return self._transition(event, SignalSide.EXIT, PositionState.FLAT)
        return []

    def _transition(
        self,
        event: MarketEvent,
        side: SignalSide,
        state: PositionState,
    ) -> list[SignalEvent]:
        self._state[event.symbol] = state
        return [SignalEvent(event.timestamp, event.symbol, side)]
