from datetime import datetime

import pytest

from quant_system.backtest import FillEvent, MarketEvent, OrderSide
from quant_system.portfolio import Account, InsufficientCashError

NOW = datetime(2026, 1, 2, 16)


def fill(
    order_id: str,
    symbol: str,
    side: OrderSide,
    quantity: int,
    price: float,
    commission: float = 5.0,
) -> FillEvent:
    return FillEvent(order_id, NOW, symbol, side, quantity, price, commission, 0.0)


def market(symbol: str, close: float) -> MarketEvent:
    return MarketEvent(NOW, symbol, close, close, close, close, 1_000.0)


def test_buy_updates_cash_and_position() -> None:
    account = Account(100_000.0)

    account.apply_fill(fill("o-1", "AAPL", OrderSide.BUY, 100, 100.0))

    assert account.cash == 89_995.0
    assert account.position("AAPL") == 100


def test_sell_updates_cash_and_position() -> None:
    account = Account(100_000.0, allow_short_selling=True)

    account.apply_fill(fill("o-1", "AAPL", OrderSide.SELL, 100, 100.0))

    assert account.cash == 109_995.0
    assert account.position("AAPL") == -100


def test_transaction_cost_example_does_not_double_count_slippage() -> None:
    account = Account(100_000.0)
    execution = FillEvent("o-1", NOW, "AAPL", OrderSide.BUY, 100, 100.10, 5.0, 10.0)

    account.apply_fill(execution)

    assert account.cash == pytest.approx(89_985.0)


def test_mark_to_market_uses_latest_close() -> None:
    account = Account(100_000.0)
    account.apply_fill(fill("o-1", "AAPL", OrderSide.BUY, 100, 100.0, commission=0.0))

    equity = account.mark_to_market(market("AAPL", 105.0))

    assert equity == 100_500.0
    assert account.equity_curve.iloc[-1] == 100_500.0


def test_mark_to_market_supports_multiple_symbols() -> None:
    account = Account(100_000.0, allow_short_selling=True)
    account.apply_fill(fill("o-1", "AAPL", OrderSide.BUY, 100, 100.0, 0.0))
    account.mark_to_market(market("AAPL", 101.0))
    account.apply_fill(fill("o-2", "MSFT", OrderSide.SELL, 50, 200.0, 0.0))

    equity = account.mark_to_market(market("MSFT", 198.0))

    assert account.positions == {"AAPL": 100, "MSFT": -50}
    assert equity == 100_200.0


def test_insufficient_cash_is_explicit() -> None:
    account = Account(1_000.0)
    expensive_fill = fill("o-1", "AAPL", OrderSide.BUY, 100, 100.0)

    assert not account.can_apply_fill(expensive_fill)
    with pytest.raises(InsufficientCashError, match="cash/short-selling constraints"):
        account.apply_fill(expensive_fill)


def test_short_sale_requires_explicit_enablement() -> None:
    account = Account(100_000.0)
    short_fill = fill("o-1", "AAPL", OrderSide.SELL, 100, 100.0)

    assert not account.can_apply_fill(short_fill)
    with pytest.raises(InsufficientCashError, match="short-selling"):
        account.apply_fill(short_fill)
