from datetime import datetime

import pytest

from quant_system.backtest import FillEvent, OrderSide
from quant_system.portfolio import Portfolio

NOW = datetime(2026, 1, 2, 16)


def fill(
    order_id: str,
    symbol: str,
    side: OrderSide,
    quantity: int,
    price: float,
    commission: float = 0.0,
    slippage: float = 0.0,
) -> FillEvent:
    return FillEvent(
        order_id,
        NOW,
        symbol,
        side,
        quantity,
        price,
        commission,
        slippage,
    )


def test_buy_cash_and_zero_cost_equity_invariant() -> None:
    portfolio = Portfolio(100_000.0)
    portfolio.process_fill(fill("o-1", "AAPL", OrderSide.BUY, 100, 50.0))
    portfolio.update_market_price("AAPL", 50.0)

    assert portfolio.cash == 95_000.0
    assert portfolio.position("AAPL") == 100
    assert portfolio.equity == 100_000.0


def test_sell_cash_accounting_with_short_enabled() -> None:
    portfolio = Portfolio(100_000.0, allow_short_selling=True)

    portfolio.process_fill(fill("o-1", "MSFT", OrderSide.SELL, 50, 200.0))

    assert portfolio.cash == 110_000.0
    assert portfolio.position("MSFT") == -50


def test_commission_is_separate_and_reduces_equity_once() -> None:
    portfolio = Portfolio(100_000.0)
    portfolio.process_fill(fill("o-1", "AAPL", OrderSide.BUY, 100, 50.0, 5.0))
    portfolio.update_market_price("AAPL", 50.0)

    assert portfolio.cash == 94_995.0
    assert portfolio.total_commission == 5.0
    assert portfolio.equity == 99_995.0
    assert portfolio.gross_realized_pnl == 0.0
    assert portfolio.net_realized_pnl == -5.0


def test_slippage_appears_as_immediate_unrealized_loss() -> None:
    portfolio = Portfolio(100_000.0)
    portfolio.process_fill(
        fill("o-1", "AAPL", OrderSide.BUY, 100, 101.0, slippage=100.0)
    )
    portfolio.update_market_price("AAPL", 100.0)

    assert portfolio.unrealized_pnl == -100.0
    assert portfolio.equity == 99_900.0


def test_partial_close_realized_and_net_realized_pnl() -> None:
    portfolio = Portfolio(100_000.0)
    portfolio.process_fill(fill("o-1", "AAPL", OrderSide.BUY, 100, 50.0, 2.0))
    portfolio.process_fill(fill("o-2", "AAPL", OrderSide.SELL, 40, 60.0, 3.0))
    portfolio.update_market_price("AAPL", 60.0)

    assert portfolio.gross_realized_pnl == 400.0
    assert portfolio.realized_pnl == 400.0
    assert portfolio.total_commission == 5.0
    assert portfolio.net_realized_pnl == 395.0
    assert portfolio.unrealized_pnl == 600.0


def test_multi_symbol_equity_pnl_and_exposure_by_hand() -> None:
    portfolio = Portfolio(100_000.0, allow_short_selling=True)
    portfolio.process_fill(fill("o-1", "AAPL", OrderSide.BUY, 100, 100.0))
    portfolio.process_fill(fill("o-2", "MSFT", OrderSide.SELL, 50, 200.0))
    portfolio.update_market_price("AAPL", 110.0)
    portfolio.update_market_price("MSFT", 190.0)

    assert portfolio.cash == 100_000.0
    assert portfolio.position("AAPL") == 100
    assert portfolio.position("MSFT") == -50
    assert portfolio.gross_realized_pnl == 0.0
    assert portfolio.unrealized_pnl == 1_500.0
    assert portfolio.gross_exposure == 20_500.0
    assert portfolio.net_exposure == 1_500.0
    assert portfolio.equity == 101_500.0
    assert portfolio.gross_exposure_pct == pytest.approx(20_500.0 / 101_500.0)
    assert portfolio.net_exposure_pct == pytest.approx(1_500.0 / 101_500.0)


def test_gross_and_net_exposure_acceptance_example() -> None:
    portfolio = Portfolio(100_000.0, allow_short_selling=True)
    portfolio.process_fill(fill("o-1", "AAPL", OrderSide.BUY, 100, 100.0))
    portfolio.process_fill(fill("o-2", "MSFT", OrderSide.SELL, 20, 200.0))
    portfolio.update_market_price("AAPL", 100.0)
    portfolio.update_market_price("MSFT", 200.0)

    assert portfolio.gross_exposure == 14_000.0
    assert portfolio.net_exposure == 6_000.0


def test_portfolio_snapshot_captures_point_in_time_state() -> None:
    portfolio = Portfolio(100_000.0)
    portfolio.process_fill(fill("o-1", "AAPL", OrderSide.BUY, 100, 100.0))
    portfolio.update_market_price("AAPL", 105.0)

    snapshot = portfolio.record_snapshot(NOW)

    assert snapshot.timestamp == NOW
    assert snapshot.cash == 90_000.0
    assert snapshot.equity == 100_500.0
    assert snapshot.realized_pnl == 0.0
    assert snapshot.unrealized_pnl == 500.0
    assert snapshot.gross_exposure == 10_500.0
    assert snapshot.net_exposure == 10_500.0
    assert portfolio.snapshots == (snapshot,)
    assert portfolio.equity_curve.iloc[0] == 100_500.0
