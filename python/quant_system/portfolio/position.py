"""Signed-quantity position accounting with average entry price."""

import math
from dataclasses import dataclass
from enum import StrEnum

from quant_system.backtest.events import OrderSide


class PositionSide(StrEnum):
    LONG = "LONG"
    SHORT = "SHORT"
    FLAT = "FLAT"


def _valid_price(price: float, *, field: str = "price") -> float:
    if isinstance(price, bool) or not isinstance(price, int | float):
        raise TypeError(f"{field} must be a real number")
    result = float(price)
    if not math.isfinite(result) or result <= 0.0:
        raise ValueError(f"{field} must be finite and greater than zero")
    return result


@dataclass(slots=True)
class Position:
    """One symbol's authoritative quantity, cost basis, and gross realized P&L."""

    symbol: str
    quantity: int = 0
    average_price: float = 0.0
    realized_pnl: float = 0.0
    round_trips: int = 0

    def __post_init__(self) -> None:
        if not isinstance(self.symbol, str) or not self.symbol.strip():
            raise ValueError("symbol must be a non-empty string")
        self.symbol = self.symbol.strip()
        if isinstance(self.quantity, bool) or not isinstance(self.quantity, int):
            raise TypeError("quantity must be an integer")
        if self.quantity == 0:
            self.average_price = 0.0
        else:
            self.average_price = _valid_price(self.average_price, field="average_price")
        if not math.isfinite(float(self.realized_pnl)):
            raise ValueError("realized_pnl must be finite")
        self.realized_pnl = float(self.realized_pnl)
        if isinstance(self.round_trips, bool) or not isinstance(self.round_trips, int):
            raise TypeError("round_trips must be an integer")
        if self.round_trips < 0:
            raise ValueError("round_trips must be non-negative")

    @property
    def is_long(self) -> bool:
        return self.quantity > 0

    @property
    def is_short(self) -> bool:
        return self.quantity < 0

    @property
    def is_flat(self) -> bool:
        return self.quantity == 0

    @property
    def side(self) -> PositionSide:
        if self.is_long:
            return PositionSide.LONG
        if self.is_short:
            return PositionSide.SHORT
        return PositionSide.FLAT

    def apply_fill(self, side: OrderSide, quantity: int, price: float) -> float:
        """Apply a fill and return its gross realized P&L contribution."""

        if not isinstance(side, OrderSide):
            raise TypeError("side must be an OrderSide")
        if isinstance(quantity, bool) or not isinstance(quantity, int):
            raise TypeError("quantity must be an integer")
        if quantity <= 0:
            raise ValueError("quantity must be positive")
        fill_price = _valid_price(price)
        signed_fill = quantity if side is OrderSide.BUY else -quantity

        if self.is_flat:
            self.quantity = signed_fill
            self.average_price = fill_price
            return 0.0

        if self.quantity * signed_fill > 0:
            existing_quantity = abs(self.quantity)
            total_quantity = existing_quantity + quantity
            self.average_price = (
                existing_quantity * self.average_price + quantity * fill_price
            ) / total_quantity
            self.quantity += signed_fill
            return 0.0

        closed_quantity = min(abs(self.quantity), quantity)
        if self.is_long:
            realized_delta = (fill_price - self.average_price) * closed_quantity
        else:
            realized_delta = (self.average_price - fill_price) * closed_quantity
        self.realized_pnl += realized_delta

        previous_quantity = self.quantity
        self.quantity += signed_fill
        if self.quantity == 0:
            self.average_price = 0.0
            self.round_trips += 1
        elif self.quantity * previous_quantity < 0:
            self.average_price = fill_price
            self.round_trips += 1
        return realized_delta

    def unrealized_pnl(self, mark_price: float) -> float:
        mark = _valid_price(mark_price, field="mark_price")
        if self.is_flat:
            return 0.0
        return (mark - self.average_price) * self.quantity

    def market_value(self, mark_price: float) -> float:
        return self.quantity * _valid_price(mark_price, field="mark_price")
