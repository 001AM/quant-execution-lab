"""Next-bar-open market-order execution simulation."""

import math
from collections import deque
from collections.abc import Callable

from quant_system.backtest.events import FillEvent, MarketEvent, OrderEvent, OrderType
from quant_system.execution.costs import CommissionModel, ZeroCommission
from quant_system.execution.slippage import NoSlippage, SlippageModel


class DuplicateOrderError(ValueError):
    """Raised when an order ID is submitted more than once in a run."""


FillAcceptance = Callable[[FillEvent], bool]


class ExecutionSimulator:
    """Fill eligible MARKET orders at the next same-symbol bar's open.

    Eligibility requires ``market.timestamp > order.timestamp``. This rule is
    enforced here so callers cannot accidentally produce same-bar fills.
    """

    def __init__(
        self,
        commission: CommissionModel | None = None,
        slippage: SlippageModel | None = None,
    ) -> None:
        self.commission = commission or ZeroCommission()
        self.slippage = slippage or NoSlippage()
        self._pending: deque[OrderEvent] = deque()
        self._seen_order_ids: set[str] = set()
        self._rejected: list[OrderEvent] = []

    @property
    def pending_orders(self) -> tuple[OrderEvent, ...]:
        return tuple(self._pending)

    @property
    def rejected_orders(self) -> tuple[OrderEvent, ...]:
        return tuple(self._rejected)

    def reset(self) -> None:
        self._pending.clear()
        self._seen_order_ids.clear()
        self._rejected.clear()

    def submit(self, order: OrderEvent) -> None:
        if order.order_id in self._seen_order_ids:
            raise DuplicateOrderError(f"duplicate order ID '{order.order_id}'")
        if order.order_type is not OrderType.MARKET:
            raise ValueError("ExecutionSimulator supports only MARKET orders")
        self._seen_order_ids.add(order.order_id)
        self._pending.append(order)

    def execute_pending(
        self,
        market: MarketEvent,
        accept_fill: FillAcceptance | None = None,
    ) -> list[FillEvent]:
        """Execute eligible orders, optionally rejecting unaffordable fills."""

        fills: list[FillEvent] = []
        still_pending: deque[OrderEvent] = deque()
        while self._pending:
            order = self._pending.popleft()
            if order.symbol != market.symbol or market.timestamp <= order.timestamp:
                still_pending.append(order)
                continue

            fill_price = self.slippage.apply(market.open, order.side)
            commission = self.commission.calculate(order.quantity, fill_price)
            monetary_slippage = math.fabs(fill_price - market.open) * order.quantity
            fill = FillEvent(
                order_id=order.order_id,
                timestamp=market.timestamp,
                symbol=order.symbol,
                side=order.side,
                quantity=order.quantity,
                fill_price=fill_price,
                commission=commission,
                slippage=monetary_slippage,
            )
            if accept_fill is not None and not accept_fill(fill):
                self._rejected.append(order)
                continue
            fills.append(fill)

        self._pending = still_pending
        return fills
