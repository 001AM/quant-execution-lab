"""Deterministic adverse fill-price models."""

import math
from dataclasses import dataclass
from typing import Protocol

from quant_system.backtest.events import OrderSide


class SlippageModel(Protocol):
    def apply(self, price: float, side: OrderSide) -> float:
        """Return a fill price no better than the reference market price."""


def _validate_price_and_side(price: float, side: OrderSide) -> float:
    if isinstance(price, bool) or not isinstance(price, int | float):
        raise TypeError("price must be a real number")
    result = float(price)
    if not math.isfinite(result) or result <= 0.0:
        raise ValueError("price must be finite and greater than zero")
    if not isinstance(side, OrderSide):
        raise TypeError("side must be an OrderSide")
    return result


@dataclass(frozen=True, slots=True)
class NoSlippage:
    def apply(self, price: float, side: OrderSide) -> float:
        return _validate_price_and_side(price, side)


@dataclass(frozen=True, slots=True)
class FixedSlippage:
    """Move the execution price adversely by a fixed per-unit amount."""

    amount: float

    def __post_init__(self) -> None:
        if isinstance(self.amount, bool) or not isinstance(self.amount, int | float):
            raise TypeError("amount must be a real number")
        if not math.isfinite(float(self.amount)) or self.amount < 0.0:
            raise ValueError("amount must be finite and non-negative")
        object.__setattr__(self, "amount", float(self.amount))

    def apply(self, price: float, side: OrderSide) -> float:
        reference = _validate_price_and_side(price, side)
        fill_price = reference + self.amount if side is OrderSide.BUY else reference - self.amount
        if fill_price <= 0.0:
            raise ValueError("fixed slippage would produce a non-positive fill price")
        return fill_price


@dataclass(frozen=True, slots=True)
class PercentageSlippage:
    """Move the execution price adversely by a fraction of market price."""

    rate: float

    def __post_init__(self) -> None:
        if isinstance(self.rate, bool) or not isinstance(self.rate, int | float):
            raise TypeError("rate must be a real number")
        if not math.isfinite(float(self.rate)) or not 0.0 <= self.rate < 1.0:
            raise ValueError("rate must be finite, non-negative, and less than 1.0")
        object.__setattr__(self, "rate", float(self.rate))

    def apply(self, price: float, side: OrderSide) -> float:
        reference = _validate_price_and_side(price, side)
        multiplier = 1.0 + self.rate if side is OrderSide.BUY else 1.0 - self.rate
        return reference * multiplier
