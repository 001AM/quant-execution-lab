"""Strategy boundary for the event-driven backtester."""

from abc import ABC, abstractmethod
from enum import StrEnum

from quant_system.backtest.events import MarketEvent, SignalEvent


class PositionState(StrEnum):
    """A strategy's intended per-symbol exposure state."""

    FLAT = "FLAT"
    LONG = "LONG"
    SHORT = "SHORT"


class Strategy(ABC):
    """Generate signals from completed market events only."""

    @abstractmethod
    def on_market(self, event: MarketEvent) -> list[SignalEvent]:
        """Return zero or more decisions for a newly available market bar."""

    def warm_up(self, event: MarketEvent) -> None:
        """Observe pre-evaluation history without emitting a tradable signal."""

        return None

    def reset(self) -> None:
        """Reset strategy state before a run, if the strategy has any."""

        return None
