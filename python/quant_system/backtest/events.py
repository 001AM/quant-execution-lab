"""Typed events and the in-process FIFO event queue."""

import math
from collections import deque
from dataclasses import dataclass
from datetime import datetime
from enum import StrEnum
from typing import ClassVar, Protocol


class EventType(StrEnum):
    """Names of events moving through the backtest pipeline."""

    MARKET = "MARKET"
    SIGNAL = "SIGNAL"
    ORDER = "ORDER"
    FILL = "FILL"


class SignalSide(StrEnum):
    BUY = "BUY"
    SELL = "SELL"
    EXIT = "EXIT"


class OrderSide(StrEnum):
    BUY = "BUY"
    SELL = "SELL"


class OrderType(StrEnum):
    MARKET = "MARKET"


class Event(Protocol):
    """Structural base abstraction shared by all backtest events."""

    timestamp: datetime
    event_type: ClassVar[EventType]


def _validate_timestamp(timestamp: datetime) -> None:
    if not isinstance(timestamp, datetime):
        raise TypeError("timestamp must be a datetime")


def _clean_required_text(value: str, *, field: str) -> str:
    if not isinstance(value, str):
        raise TypeError(f"{field} must be a string")
    cleaned = value.strip()
    if not cleaned:
        raise ValueError(f"{field} must not be empty")
    return cleaned


def _finite_number(value: float, *, field: str) -> float:
    if isinstance(value, bool) or not isinstance(value, int | float):
        raise TypeError(f"{field} must be a real number")
    result = float(value)
    if not math.isfinite(result):
        raise ValueError(f"{field} must be finite")
    return result


def _positive_quantity(quantity: int) -> None:
    if isinstance(quantity, bool) or not isinstance(quantity, int):
        raise TypeError("quantity must be an integer")
    if quantity <= 0:
        raise ValueError("quantity must be positive")


@dataclass(frozen=True, slots=True)
class MarketEvent:
    """A completed bar made available to a strategy at ``timestamp``."""

    timestamp: datetime
    symbol: str
    open: float
    high: float
    low: float
    close: float
    volume: float

    event_type: ClassVar[EventType] = EventType.MARKET

    def __post_init__(self) -> None:
        _validate_timestamp(self.timestamp)
        object.__setattr__(self, "symbol", _clean_required_text(self.symbol, field="symbol"))
        for field in ("open", "high", "low", "close", "volume"):
            object.__setattr__(
                self,
                field,
                _finite_number(getattr(self, field), field=field),
            )
        if min(self.open, self.high, self.low, self.close) <= 0.0:
            raise ValueError("OHLC prices must be greater than zero")
        if self.volume < 0.0:
            raise ValueError("volume must be non-negative")
        if self.high < max(self.open, self.low, self.close):
            raise ValueError("high must be greater than or equal to open, low, and close")
        if self.low > min(self.open, self.high, self.close):
            raise ValueError("low must be less than or equal to open, high, and close")


@dataclass(frozen=True, slots=True)
class SignalEvent:
    """A strategy decision that does not modify orders or account state."""

    timestamp: datetime
    symbol: str
    side: SignalSide
    strength: float = 1.0

    event_type: ClassVar[EventType] = EventType.SIGNAL

    def __post_init__(self) -> None:
        _validate_timestamp(self.timestamp)
        object.__setattr__(self, "symbol", _clean_required_text(self.symbol, field="symbol"))
        if not isinstance(self.side, SignalSide):
            raise TypeError("side must be a SignalSide")
        strength = _finite_number(self.strength, field="strength")
        if not 0.0 < strength <= 1.0:
            raise ValueError("strength must be greater than zero and at most 1.0")
        object.__setattr__(self, "strength", strength)


@dataclass(frozen=True, slots=True)
class OrderEvent:
    """An immutable request for execution."""

    order_id: str
    timestamp: datetime
    symbol: str
    side: OrderSide
    quantity: int
    order_type: OrderType = OrderType.MARKET

    event_type: ClassVar[EventType] = EventType.ORDER

    def __post_init__(self) -> None:
        object.__setattr__(self, "order_id", _clean_required_text(self.order_id, field="order_id"))
        _validate_timestamp(self.timestamp)
        object.__setattr__(self, "symbol", _clean_required_text(self.symbol, field="symbol"))
        if not isinstance(self.side, OrderSide):
            raise TypeError("side must be an OrderSide")
        _positive_quantity(self.quantity)
        if not isinstance(self.order_type, OrderType):
            raise TypeError("order_type must be an OrderType")
        if self.order_type is not OrderType.MARKET:
            raise ValueError("only MARKET orders are supported")


@dataclass(frozen=True, slots=True)
class FillEvent:
    """An executed order with monetary commission and slippage costs.

    ``slippage`` is the total monetary impact relative to the reference market
    price: ``abs(fill_price - reference_price) * quantity``. It is already
    embedded in ``fill_price`` and must not be deducted from cash a second time.
    """

    order_id: str
    timestamp: datetime
    symbol: str
    side: OrderSide
    quantity: int
    fill_price: float
    commission: float
    slippage: float

    event_type: ClassVar[EventType] = EventType.FILL

    def __post_init__(self) -> None:
        object.__setattr__(self, "order_id", _clean_required_text(self.order_id, field="order_id"))
        _validate_timestamp(self.timestamp)
        object.__setattr__(self, "symbol", _clean_required_text(self.symbol, field="symbol"))
        if not isinstance(self.side, OrderSide):
            raise TypeError("side must be an OrderSide")
        _positive_quantity(self.quantity)
        for field in ("fill_price", "commission", "slippage"):
            object.__setattr__(
                self,
                field,
                _finite_number(getattr(self, field), field=field),
            )
        if self.fill_price <= 0.0:
            raise ValueError("fill_price must be greater than zero")
        if self.commission < 0.0:
            raise ValueError("commission must be non-negative")
        if self.slippage < 0.0:
            raise ValueError("slippage must be non-negative")


type EventMessage = MarketEvent | SignalEvent | OrderEvent | FillEvent


class EventQueue:
    """A minimal deterministic FIFO queue for backtest events."""

    def __init__(self) -> None:
        self._events: deque[EventMessage] = deque()

    def put(self, event: EventMessage) -> None:
        self._events.append(event)

    def get(self) -> EventMessage:
        if not self._events:
            raise IndexError("cannot get an event from an empty queue")
        return self._events.popleft()

    def clear(self) -> None:
        self._events.clear()

    def __bool__(self) -> bool:
        return bool(self._events)

    def __len__(self) -> int:
        return len(self._events)
