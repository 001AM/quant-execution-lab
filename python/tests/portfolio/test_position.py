import pytest

from quant_system.backtest import OrderSide
from quant_system.portfolio import Position, PositionSide


def test_open_long() -> None:
    position = Position("AAPL")

    realized = position.apply_fill(OrderSide.BUY, 100, 50.0)

    assert position.quantity == 100
    assert position.average_price == 50.0
    assert position.realized_pnl == 0.0
    assert realized == 0.0
    assert position.is_long
    assert position.side is PositionSide.LONG


def test_increase_long_uses_weighted_average() -> None:
    position = Position("AAPL")
    position.apply_fill(OrderSide.BUY, 100, 50.0)

    position.apply_fill(OrderSide.BUY, 100, 60.0)

    assert position.quantity == 200
    assert position.average_price == pytest.approx(55.0)
    assert position.realized_pnl == 0.0


@pytest.mark.parametrize(
    "exit_price, expected_pnl",
    [(60.0, 400.0), (40.0, -400.0)],
)
def test_partial_long_close_preserves_average(
    exit_price: float, expected_pnl: float
) -> None:
    position = Position("AAPL")
    position.apply_fill(OrderSide.BUY, 100, 50.0)

    realized = position.apply_fill(OrderSide.SELL, 40, exit_price)

    assert position.quantity == 60
    assert position.average_price == 50.0
    assert realized == pytest.approx(expected_pnl)
    assert position.realized_pnl == pytest.approx(expected_pnl)


def test_full_long_close_resets_average() -> None:
    position = Position("AAPL")
    position.apply_fill(OrderSide.BUY, 100, 50.0)

    position.apply_fill(OrderSide.SELL, 100, 60.0)

    assert position.quantity == 0
    assert position.average_price == 0.0
    assert position.realized_pnl == 1_000.0
    assert position.is_flat
    assert position.round_trips == 1


def test_open_short() -> None:
    position = Position("AAPL")

    position.apply_fill(OrderSide.SELL, 100, 50.0)

    assert position.quantity == -100
    assert position.average_price == 50.0
    assert position.is_short
    assert position.side is PositionSide.SHORT


def test_increase_short_uses_absolute_weighted_average() -> None:
    position = Position("AAPL")
    position.apply_fill(OrderSide.SELL, 100, 50.0)

    position.apply_fill(OrderSide.SELL, 100, 40.0)

    assert position.quantity == -200
    assert position.average_price == pytest.approx(45.0)


@pytest.mark.parametrize(
    "cover_price, expected_pnl",
    [(40.0, 400.0), (60.0, -400.0)],
)
def test_partial_short_cover_preserves_average(
    cover_price: float, expected_pnl: float
) -> None:
    position = Position("AAPL")
    position.apply_fill(OrderSide.SELL, 100, 50.0)

    realized = position.apply_fill(OrderSide.BUY, 40, cover_price)

    assert position.quantity == -60
    assert position.average_price == 50.0
    assert realized == pytest.approx(expected_pnl)
    assert position.realized_pnl == pytest.approx(expected_pnl)


def test_losing_full_short_cover() -> None:
    position = Position("AAPL")
    position.apply_fill(OrderSide.SELL, 100, 50.0)

    position.apply_fill(OrderSide.BUY, 100, 60.0)

    assert position.is_flat
    assert position.average_price == 0.0
    assert position.realized_pnl == -1_000.0


def test_long_to_short_reversal() -> None:
    position = Position("AAPL")
    position.apply_fill(OrderSide.BUY, 100, 50.0)

    realized = position.apply_fill(OrderSide.SELL, 150, 60.0)

    assert realized == 1_000.0
    assert position.realized_pnl == 1_000.0
    assert position.quantity == -50
    assert position.average_price == 60.0
    assert position.round_trips == 1


def test_short_to_long_reversal() -> None:
    position = Position("AAPL")
    position.apply_fill(OrderSide.SELL, 100, 60.0)

    realized = position.apply_fill(OrderSide.BUY, 150, 50.0)

    assert realized == 1_000.0
    assert position.realized_pnl == 1_000.0
    assert position.quantity == 50
    assert position.average_price == 50.0
    assert position.round_trips == 1


@pytest.mark.parametrize(
    "side, entry, mark, expected",
    [
        (OrderSide.BUY, 50.0, 55.0, 500.0),
        (OrderSide.BUY, 50.0, 45.0, -500.0),
        (OrderSide.SELL, 50.0, 45.0, 500.0),
        (OrderSide.SELL, 50.0, 55.0, -500.0),
    ],
)
def test_unrealized_pnl(
    side: OrderSide, entry: float, mark: float, expected: float
) -> None:
    position = Position("AAPL")
    position.apply_fill(side, 100, entry)

    assert position.unrealized_pnl(mark) == pytest.approx(expected)


def test_signed_market_value() -> None:
    long = Position("AAPL")
    short = Position("MSFT")
    long.apply_fill(OrderSide.BUY, 100, 50.0)
    short.apply_fill(OrderSide.SELL, 100, 50.0)

    assert long.market_value(50.0) == 5_000.0
    assert short.market_value(50.0) == -5_000.0
