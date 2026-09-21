from datetime import datetime, timedelta

import pytest

from quant_system.backtest import (
    EventQueue,
    EventType,
    FillEvent,
    FixedSizeOrderGenerator,
    MarketEvent,
    OrderEvent,
    OrderSide,
    OrderType,
    SignalEvent,
    SignalSide,
)

NOW = datetime(2026, 1, 2, 16, 0)


def market_event() -> MarketEvent:
    return MarketEvent(NOW, "AAPL", 100.0, 102.0, 99.0, 101.0, 1_000.0)


def test_market_event_construction_and_type() -> None:
    event = market_event()

    assert event.event_type is EventType.MARKET
    assert event.symbol == "AAPL"
    assert event.close == 101.0


@pytest.mark.parametrize(
    "kwargs, message",
    [
        ({"high": 98.0}, "high"),
        ({"low": 103.0}, "low"),
        ({"open": 0.0}, "greater than zero"),
        ({"volume": -1.0}, "non-negative"),
    ],
)
def test_market_event_validates_financial_values(
    kwargs: dict[str, float], message: str
) -> None:
    values = {
        "timestamp": NOW,
        "symbol": "AAPL",
        "open": 100.0,
        "high": 102.0,
        "low": 99.0,
        "close": 101.0,
        "volume": 1_000.0,
    }
    values.update(kwargs)

    with pytest.raises(ValueError, match=message):
        MarketEvent(**values)  # type: ignore[arg-type]


def test_signal_event_uses_enum_and_validates_strength() -> None:
    event = SignalEvent(NOW, "AAPL", SignalSide.BUY)

    assert event.event_type is EventType.SIGNAL
    assert event.side.value == "BUY"
    assert event.strength == 1.0

    with pytest.raises(ValueError, match="strength"):
        SignalEvent(NOW, "AAPL", SignalSide.SELL, strength=0.0)


def test_signal_event_rejects_arbitrary_side_string() -> None:
    with pytest.raises(TypeError, match="SignalSide"):
        SignalEvent(NOW, "AAPL", "BUY")  # type: ignore[arg-type]


def test_order_event_validates_quantity_and_type() -> None:
    order = OrderEvent("o-1", NOW, "AAPL", OrderSide.BUY, 100)

    assert order.event_type is EventType.ORDER
    assert order.order_type is OrderType.MARKET

    with pytest.raises(ValueError, match="positive"):
        OrderEvent("o-2", NOW, "AAPL", OrderSide.BUY, 0)


def test_fill_event_validates_execution_values() -> None:
    fill = FillEvent("o-1", NOW, "AAPL", OrderSide.BUY, 100, 100.1, 5.0, 10.0)

    assert fill.event_type is EventType.FILL
    assert fill.slippage == 10.0

    with pytest.raises(ValueError, match="commission"):
        FillEvent("o-2", NOW, "AAPL", OrderSide.BUY, 100, 100.0, -1.0, 0.0)


def test_event_queue_is_fifo() -> None:
    queue = EventQueue()
    market = market_event()
    signal = SignalEvent(NOW, "AAPL", SignalSide.BUY)
    order = OrderEvent("o-1", NOW, "AAPL", OrderSide.BUY, 100)
    fill = FillEvent("o-1", NOW + timedelta(days=1), "AAPL", OrderSide.BUY, 100, 101, 0, 0)

    for event in (market, signal, order, fill):
        queue.put(event)

    assert [queue.get(), queue.get(), queue.get(), queue.get()] == [
        market,
        signal,
        order,
        fill,
    ]
    assert not queue
    with pytest.raises(IndexError):
        queue.get()


@pytest.mark.parametrize(
    "side, position, expected_side, expected_quantity",
    [
        (SignalSide.BUY, 0, OrderSide.BUY, 100),
        (SignalSide.SELL, 0, OrderSide.SELL, 100),
        (SignalSide.EXIT, 150, OrderSide.SELL, 150),
        (SignalSide.EXIT, -75, OrderSide.BUY, 75),
    ],
)
def test_fixed_order_generation(
    side: SignalSide,
    position: int,
    expected_side: OrderSide,
    expected_quantity: int,
) -> None:
    generator = FixedSizeOrderGenerator(100)

    order = generator.generate(SignalEvent(NOW, "AAPL", side), position)

    assert order is not None
    assert order.side is expected_side
    assert order.quantity == expected_quantity


def test_exit_while_flat_generates_no_order() -> None:
    generator = FixedSizeOrderGenerator(100)

    assert generator.generate(SignalEvent(NOW, "AAPL", SignalSide.EXIT), 0) is None

