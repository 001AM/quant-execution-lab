from datetime import datetime

import pytest

from quant_system.backtest import FillEvent, OrderEvent, OrderSide
from quant_system.portfolio import Portfolio
from quant_system.risk import RiskLimits, RiskManager, RiskRejectReason

NOW = datetime(2026, 1, 2, 16)


def order(side: OrderSide, quantity: int, symbol: str = "AAPL") -> OrderEvent:
    return OrderEvent("new-order", NOW, symbol, side, quantity)


def seed_fill(
    order_id: str,
    symbol: str,
    side: OrderSide,
    quantity: int,
    price: float,
) -> FillEvent:
    return FillEvent(order_id, NOW, symbol, side, quantity, price, 0.0, 0.0)


def marked_portfolio(
    initial_cash: float = 100_000.0,
    *,
    allow_short_selling: bool = False,
) -> Portfolio:
    portfolio = Portfolio(initial_cash, allow_short_selling=allow_short_selling)
    portfolio.update_market_price("AAPL", 100.0)
    return portfolio


def test_valid_order_is_approved() -> None:
    portfolio = marked_portfolio()
    manager = RiskManager(RiskLimits(max_gross_exposure=50_000.0))

    decision = manager.validate_order(order(OrderSide.BUY, 100), portfolio, 100.0)

    assert decision.approved
    assert decision.reason is None


def test_max_position_notional_violation_has_stable_reason() -> None:
    portfolio = marked_portfolio()
    manager = RiskManager(RiskLimits(max_position_notional=20_000.0))

    decision = manager.validate_order(order(OrderSide.BUY, 300), portfolio, 100.0)

    assert not decision.approved
    assert decision.reason is RiskRejectReason.MAX_POSITION_NOTIONAL


def test_max_gross_exposure_uses_all_symbols() -> None:
    portfolio = Portfolio(200_000.0, allow_short_selling=True)
    portfolio.process_fill(seed_fill("a", "AAPL", OrderSide.BUY, 500, 100.0))
    portfolio.process_fill(seed_fill("m", "MSFT", OrderSide.SELL, 150, 200.0))
    portfolio.update_market_price("AAPL", 100.0)
    portfolio.update_market_price("MSFT", 200.0)
    manager = RiskManager(RiskLimits(max_gross_exposure=100_000.0))

    decision = manager.validate_order(order(OrderSide.BUY, 300), portfolio, 100.0)

    assert portfolio.gross_exposure == 80_000.0
    assert not decision.approved
    assert decision.reason is RiskRejectReason.MAX_GROSS_EXPOSURE


def test_max_net_exposure_is_absolute() -> None:
    portfolio = marked_portfolio()
    manager = RiskManager(RiskLimits(max_net_exposure=50_000.0))

    decision = manager.validate_order(order(OrderSide.BUY, 600), portfolio, 100.0)

    assert not decision.approved
    assert decision.reason is RiskRejectReason.MAX_NET_EXPOSURE


@pytest.mark.parametrize(
    "limits, expected_reason",
    [
        (RiskLimits(max_gross_exposure_pct=0.50), RiskRejectReason.MAX_GROSS_EXPOSURE_PCT),
        (RiskLimits(max_position_pct=0.50), RiskRejectReason.MAX_POSITION_PCT),
    ],
)
def test_percentage_limits(
    limits: RiskLimits,
    expected_reason: RiskRejectReason,
) -> None:
    portfolio = marked_portfolio()

    decision = RiskManager(limits).validate_order(
        order(OrderSide.BUY, 600),
        portfolio,
        100.0,
    )

    assert not decision.approved
    assert decision.reason is expected_reason


def test_risk_reducing_order_is_allowed_while_over_limit() -> None:
    portfolio = Portfolio(200_000.0)
    portfolio.process_fill(seed_fill("a", "AAPL", OrderSide.BUY, 1_000, 100.0))
    portfolio.update_market_price("AAPL", 100.0)
    manager = RiskManager(RiskLimits(max_gross_exposure=75_000.0))

    decision = manager.validate_order(order(OrderSide.SELL, 500), portfolio, 100.0)

    assert portfolio.gross_exposure == 100_000.0
    assert decision.approved


def test_reversal_uses_final_hypothetical_position() -> None:
    portfolio = Portfolio(100_000.0, allow_short_selling=True)
    portfolio.process_fill(seed_fill("a", "AAPL", OrderSide.BUY, 100, 100.0))
    portfolio.update_market_price("AAPL", 100.0)
    manager = RiskManager(RiskLimits(max_position_notional=15_000.0))

    decision = manager.validate_order(order(OrderSide.SELL, 300), portfolio, 100.0)

    assert not decision.approved
    assert decision.reason is RiskRejectReason.MAX_POSITION_NOTIONAL
    assert portfolio.position("AAPL") == 100


def test_rejected_order_does_not_modify_portfolio() -> None:
    portfolio = marked_portfolio()
    manager = RiskManager(RiskLimits(max_position_notional=5_000.0))
    cash_before = portfolio.cash

    decision = manager.validate_order(order(OrderSide.BUY, 100), portfolio, 100.0)

    assert not decision.approved
    assert portfolio.cash == cash_before
    assert portfolio.position("AAPL") == 0
    assert portfolio.snapshots == ()


def test_short_creation_is_rejected_when_disabled() -> None:
    portfolio = marked_portfolio()

    decision = RiskManager().validate_order(
        order(OrderSide.SELL, 100),
        portfolio,
        100.0,
    )

    assert not decision.approved
    assert decision.reason is RiskRejectReason.SHORT_SELLING_DISABLED


def test_insufficient_cash_is_pretrade_rejection() -> None:
    portfolio = marked_portfolio(initial_cash=1_000.0)

    decision = RiskManager().validate_order(
        order(OrderSide.BUY, 100),
        portfolio,
        100.0,
    )

    assert not decision.approved
    assert decision.reason is RiskRejectReason.INSUFFICIENT_CASH
