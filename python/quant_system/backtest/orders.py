"""Conversion of strategy signals into fixed-size orders."""

from quant_system.backtest.events import OrderEvent, OrderSide, OrderType, SignalEvent, SignalSide


class FixedSizeOrderGenerator:
    """Generate deterministic market orders without changing account state."""

    def __init__(self, quantity: int) -> None:
        if isinstance(quantity, bool) or not isinstance(quantity, int):
            raise TypeError("quantity must be an integer")
        if quantity <= 0:
            raise ValueError("quantity must be positive")
        self.quantity = quantity
        self._next_order_number = 1

    def reset(self) -> None:
        self._next_order_number = 1

    def generate(self, signal: SignalEvent, current_position: int) -> OrderEvent | None:
        """Convert a signal, returning ``None`` for EXIT while flat."""

        if signal.side is SignalSide.BUY:
            side = OrderSide.BUY
            quantity = self.quantity
        elif signal.side is SignalSide.SELL:
            side = OrderSide.SELL
            quantity = self.quantity
        elif current_position > 0:
            side = OrderSide.SELL
            quantity = current_position
        elif current_position < 0:
            side = OrderSide.BUY
            quantity = abs(current_position)
        else:
            return None

        order = OrderEvent(
            order_id=f"order-{self._next_order_number:06d}",
            timestamp=signal.timestamp,
            symbol=signal.symbol,
            side=side,
            quantity=quantity,
            order_type=OrderType.MARKET,
        )
        self._next_order_number += 1
        return order

