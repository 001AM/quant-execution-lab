"""Typed domain models and canonical OHLCV schema constants."""

from dataclasses import dataclass
from datetime import datetime
from typing import Final

TIMESTAMP: Final = "timestamp"
SYMBOL: Final = "symbol"
OPEN: Final = "open"
HIGH: Final = "high"
LOW: Final = "low"
CLOSE: Final = "close"
VOLUME: Final = "volume"

CANONICAL_COLUMNS: Final[tuple[str, ...]] = (
    TIMESTAMP,
    SYMBOL,
    OPEN,
    HIGH,
    LOW,
    CLOSE,
    VOLUME,
)
PRICE_COLUMNS: Final[tuple[str, ...]] = (OPEN, HIGH, LOW, CLOSE)
NUMERIC_COLUMNS: Final[tuple[str, ...]] = (*PRICE_COLUMNS, VOLUME)


@dataclass(frozen=True, slots=True)
class OHLCVBar:
    """A single immutable bar in the system's canonical representation."""

    timestamp: datetime
    symbol: str
    open: float
    high: float
    low: float
    close: float
    volume: float

