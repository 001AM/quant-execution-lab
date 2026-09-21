"""Incremental rolling Z-score mean-reversion strategy."""

import math
import statistics
from collections import deque
from dataclasses import dataclass
from datetime import datetime

from quant_system.backtest.events import MarketEvent, SignalEvent, SignalSide
from quant_system.strategy.base import PositionState, Strategy


@dataclass(frozen=True, slots=True)
class MeanReversionDiagnostic:
    timestamp: datetime
    rolling_mean: float
    rolling_std: float
    z_score: float


class MeanReversionStrategy(Strategy):
    """Trade deviations from a rolling mean using population standard deviation.

    A zero standard deviation records a zero Z-score and emits no signal.
    LONG exits at ``z >= -exit_z`` and SHORT exits at ``z <= exit_z``.
    """

    def __init__(self, window: int, entry_z: float, exit_z: float) -> None:
        if isinstance(window, bool) or not isinstance(window, int):
            raise TypeError("window must be an integer")
        if window < 2:
            raise ValueError("window must be at least 2")
        for name, value in (("entry_z", entry_z), ("exit_z", exit_z)):
            if isinstance(value, bool) or not isinstance(value, int | float):
                raise TypeError(f"{name} must be a real number")
            if not math.isfinite(float(value)):
                raise ValueError(f"{name} must be finite")
        if entry_z <= 0.0:
            raise ValueError("entry_z must be greater than zero")
        if exit_z < 0.0 or exit_z >= entry_z:
            raise ValueError("exit_z must be non-negative and lower than entry_z")

        self.window = window
        self.entry_z = float(entry_z)
        self.exit_z = float(exit_z)
        self._history: dict[str, deque[float]] = {}
        self._state: dict[str, PositionState] = {}
        self._diagnostics: dict[str, MeanReversionDiagnostic] = {}

    def reset(self) -> None:
        self._history.clear()
        self._state.clear()
        self._diagnostics.clear()

    def state_for(self, symbol: str) -> PositionState:
        return self._state.get(symbol, PositionState.FLAT)

    def diagnostic_for(self, symbol: str) -> MeanReversionDiagnostic | None:
        return self._diagnostics.get(symbol)

    def on_market(self, event: MarketEvent) -> list[SignalEvent]:
        return self._observe(event, emit_signals=True)

    def warm_up(self, event: MarketEvent) -> None:
        self._observe(event, emit_signals=False)

    def _observe(self, event: MarketEvent, *, emit_signals: bool) -> list[SignalEvent]:
        history = self._history.setdefault(event.symbol, deque(maxlen=self.window))
        history.append(event.close)
        if len(history) < self.window:
            return []

        rolling_mean = statistics.fmean(history)
        rolling_std = statistics.pstdev(history)
        z_score = 0.0 if rolling_std == 0.0 else (event.close - rolling_mean) / rolling_std
        self._diagnostics[event.symbol] = MeanReversionDiagnostic(
            event.timestamp,
            rolling_mean,
            rolling_std,
            z_score,
        )
        if rolling_std == 0.0 or not emit_signals:
            return []

        state = self.state_for(event.symbol)
        if state is PositionState.FLAT and z_score <= -self.entry_z:
            return self._transition(event, SignalSide.BUY, PositionState.LONG)
        if state is PositionState.FLAT and z_score >= self.entry_z:
            return self._transition(event, SignalSide.SELL, PositionState.SHORT)
        if state is PositionState.LONG and z_score >= -self.exit_z:
            return self._transition(event, SignalSide.EXIT, PositionState.FLAT)
        if state is PositionState.SHORT and z_score <= self.exit_z:
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
