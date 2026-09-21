"""A deterministic signal schedule for examples and pipeline tests."""

from collections import defaultdict
from collections.abc import Iterable
from datetime import datetime

from quant_system.backtest.events import MarketEvent, SignalEvent
from quant_system.strategy.base import Strategy


class ScheduledSignalStrategy(Strategy):
    """Emit predeclared signals when their symbol and timestamp bar arrives.

    This class is a test harness for the event pipeline, not a trading strategy.
    """

    def __init__(self, signals: Iterable[SignalEvent]) -> None:
        schedule: dict[tuple[str, datetime], list[SignalEvent]] = defaultdict(list)
        for signal in signals:
            schedule[(signal.symbol, signal.timestamp)].append(signal)
        self._schedule = dict(schedule)

    def on_market(self, event: MarketEvent) -> list[SignalEvent]:
        return list(self._schedule.get((event.symbol, event.timestamp), ()))

