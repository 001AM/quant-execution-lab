from datetime import datetime, timedelta

import pytest

from quant_system.backtest import MarketEvent, OrderEvent, OrderSide
from quant_system.execution import (
    DuplicateOrderError,
    ExecutionSimulator,
    FixedCommission,
    PercentageSlippage,
)

DAY_1 = datetime(2026, 1, 1, 16)


def market(timestamp: datetime, symbol: str = "AAPL", open_price: float = 100.0) -> MarketEvent:
    return MarketEvent(
        timestamp,
        symbol,
        open_price,
        open_price + 2.0,
        open_price - 1.0,
        open_price + 1.0,
        1_000.0,
    )


def order(side: OrderSide = OrderSide.BUY, order_id: str = "o-1") -> OrderEvent:
    return OrderEvent(order_id, DAY_1, "AAPL", side, 100)


def test_market_buy_fills_only_at_next_bar_open() -> None:
    simulator = ExecutionSimulator()
    simulator.submit(order())

    assert simulator.execute_pending(market(DAY_1, open_price=99.0)) == []
    fills = simulator.execute_pending(market(DAY_1 + timedelta(days=1), open_price=104.0))

    assert len(fills) == 1
    assert fills[0].fill_price == 104.0
    assert fills[0].timestamp == DAY_1 + timedelta(days=1)


def test_market_sell_fills_at_next_open() -> None:
    simulator = ExecutionSimulator()
    simulator.submit(order(OrderSide.SELL))

    fill = simulator.execute_pending(market(DAY_1 + timedelta(days=1), open_price=104.0))[0]

    assert fill.side is OrderSide.SELL
    assert fill.fill_price == 104.0


def test_execution_applies_slippage_commission_and_records_monetary_impact() -> None:
    simulator = ExecutionSimulator(
        commission=FixedCommission(5.0),
        slippage=PercentageSlippage(0.001),
    )
    simulator.submit(order())

    fill = simulator.execute_pending(market(DAY_1 + timedelta(days=1), open_price=100.0))[0]

    assert fill.fill_price == pytest.approx(100.10)
    assert fill.commission == 5.0
    assert fill.slippage == pytest.approx(10.0)


def test_order_waits_for_next_bar_of_same_symbol() -> None:
    simulator = ExecutionSimulator()
    simulator.submit(order())

    assert simulator.execute_pending(market(DAY_1 + timedelta(days=1), "MSFT")) == []
    assert simulator.pending_orders == (order(),)


def test_duplicate_order_ids_are_rejected() -> None:
    simulator = ExecutionSimulator()
    simulator.submit(order())

    with pytest.raises(DuplicateOrderError, match="duplicate"):
        simulator.submit(order())


def test_fill_acceptance_can_reject_order_without_creating_fill() -> None:
    simulator = ExecutionSimulator()
    submitted = order()
    simulator.submit(submitted)

    fills = simulator.execute_pending(
        market(DAY_1 + timedelta(days=1)),
        accept_fill=lambda fill: False,
    )

    assert fills == []
    assert simulator.pending_orders == ()
    assert simulator.rejected_orders == (submitted,)

