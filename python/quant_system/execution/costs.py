"""Currency-agnostic commission models."""

import math
from dataclasses import dataclass
from typing import Protocol


class CommissionModel(Protocol):
    def calculate(self, quantity: int, price: float) -> float:
        """Return the non-negative monetary commission for an execution."""


def _validate_execution(quantity: int, price: float) -> None:
    if isinstance(quantity, bool) or not isinstance(quantity, int):
        raise TypeError("quantity must be an integer")
    if quantity <= 0:
        raise ValueError("quantity must be positive")
    if isinstance(price, bool) or not isinstance(price, int | float):
        raise TypeError("price must be a real number")
    if not math.isfinite(float(price)) or price <= 0.0:
        raise ValueError("price must be finite and greater than zero")


@dataclass(frozen=True, slots=True)
class ZeroCommission:
    def calculate(self, quantity: int, price: float) -> float:
        _validate_execution(quantity, price)
        return 0.0


@dataclass(frozen=True, slots=True)
class FixedCommission:
    """Charge a fixed monetary amount per filled order."""

    amount: float

    def __post_init__(self) -> None:
        if isinstance(self.amount, bool) or not isinstance(self.amount, int | float):
            raise TypeError("amount must be a real number")
        if not math.isfinite(float(self.amount)) or self.amount < 0.0:
            raise ValueError("amount must be finite and non-negative")
        object.__setattr__(self, "amount", float(self.amount))

    def calculate(self, quantity: int, price: float) -> float:
        _validate_execution(quantity, price)
        return self.amount


@dataclass(frozen=True, slots=True)
class PercentageCommission:
    """Charge ``rate * abs(quantity * price)`` per filled order."""

    rate: float

    def __post_init__(self) -> None:
        if isinstance(self.rate, bool) or not isinstance(self.rate, int | float):
            raise TypeError("rate must be a real number")
        if not math.isfinite(float(self.rate)) or self.rate < 0.0:
            raise ValueError("rate must be finite and non-negative")
        object.__setattr__(self, "rate", float(self.rate))

    def calculate(self, quantity: int, price: float) -> float:
        _validate_execution(quantity, price)
        return float(quantity * price * self.rate)
