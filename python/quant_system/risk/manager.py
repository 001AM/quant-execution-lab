"""Pre-trade checks against hypothetical post-order exposure."""

import math
from dataclasses import dataclass
from datetime import datetime
from enum import StrEnum

from quant_system.backtest.events import OrderEvent, OrderSide
from quant_system.portfolio.portfolio import Portfolio
from quant_system.risk.limits import RiskLimits


class RiskRejectReason(StrEnum):
    MAX_POSITION_NOTIONAL = "MAX_POSITION_NOTIONAL"
    MAX_GROSS_EXPOSURE = "MAX_GROSS_EXPOSURE"
    MAX_NET_EXPOSURE = "MAX_NET_EXPOSURE"
    MAX_GROSS_EXPOSURE_PCT = "MAX_GROSS_EXPOSURE_PCT"
    MAX_POSITION_PCT = "MAX_POSITION_PCT"
    SHORT_SELLING_DISABLED = "SHORT_SELLING_DISABLED"
    INSUFFICIENT_CASH = "INSUFFICIENT_CASH"
    NON_POSITIVE_EQUITY = "NON_POSITIVE_EQUITY"


@dataclass(frozen=True, slots=True)
class RiskDecision:
    approved: bool
    reason: RiskRejectReason | None = None
    message: str | None = None


@dataclass(frozen=True, slots=True)
class RejectedOrder:
    order: OrderEvent
    timestamp: datetime
    reason: RiskRejectReason
    message: str


@dataclass(frozen=True, slots=True)
class _ExposureState:
    position_notional: float
    gross_exposure: float
    absolute_net_exposure: float
    position_pct: float | None
    gross_exposure_pct: float | None


class RiskManager:
    """Approve or reject orders without mutating the portfolio."""

    def __init__(self, limits: RiskLimits | None = None) -> None:
        self.limits = limits or RiskLimits()

    def validate_order(
        self,
        order: OrderEvent,
        portfolio: Portfolio,
        market_price: float,
    ) -> RiskDecision:
        if isinstance(market_price, bool) or not isinstance(market_price, int | float):
            raise TypeError("market_price must be a real number")
        price = float(market_price)
        if not math.isfinite(price) or price <= 0.0:
            raise ValueError("market_price must be finite and greater than zero")

        current_quantity = portfolio.position(order.symbol)
        quantity_change = order.quantity if order.side is OrderSide.BUY else -order.quantity
        hypothetical_quantity = current_quantity + quantity_change

        if (
            not portfolio.allow_short_selling
            and hypothetical_quantity < 0
            and abs(hypothetical_quantity) > abs(min(current_quantity, 0))
        ):
            return self._reject(
                RiskRejectReason.SHORT_SELLING_DISABLED,
                f"order would create or increase a short {order.symbol} position",
            )

        if order.side is OrderSide.BUY and order.quantity * price > portfolio.cash:
            return self._reject(
                RiskRejectReason.INSUFFICIENT_CASH,
                "order notional exceeds available cash at the pre-trade market price",
            )

        before = self._exposure_state(
            portfolio,
            order.symbol,
            current_quantity,
            price,
        )
        after = self._exposure_state(
            portfolio,
            order.symbol,
            hypothetical_quantity,
            price,
        )

        checks = (
            (
                RiskRejectReason.MAX_POSITION_NOTIONAL,
                before.position_notional,
                after.position_notional,
                self.limits.max_position_notional,
            ),
            (
                RiskRejectReason.MAX_GROSS_EXPOSURE,
                before.gross_exposure,
                after.gross_exposure,
                self.limits.max_gross_exposure,
            ),
            (
                RiskRejectReason.MAX_NET_EXPOSURE,
                before.absolute_net_exposure,
                after.absolute_net_exposure,
                self.limits.max_net_exposure,
            ),
            (
                RiskRejectReason.MAX_POSITION_PCT,
                before.position_pct,
                after.position_pct,
                self.limits.max_position_pct,
            ),
            (
                RiskRejectReason.MAX_GROSS_EXPOSURE_PCT,
                before.gross_exposure_pct,
                after.gross_exposure_pct,
                self.limits.max_gross_exposure_pct,
            ),
        )
        for reason, before_value, after_value, limit in checks:
            if limit is None:
                continue
            if before_value is None or after_value is None:
                return self._reject(
                    RiskRejectReason.NON_POSITIVE_EQUITY,
                    "percentage exposure limits require positive portfolio equity",
                )
            if after_value > limit and after_value > before_value:
                return self._reject(
                    reason,
                    f"hypothetical value {after_value:.6f} exceeds limit {limit:.6f}",
                )
        return RiskDecision(approved=True)

    def _exposure_state(
        self,
        portfolio: Portfolio,
        target_symbol: str,
        target_quantity: int,
        target_price: float,
    ) -> _ExposureState:
        signed_values: list[float] = []
        symbols = set(portfolio.positions) | {target_symbol}
        for symbol in symbols:
            quantity = (
                target_quantity
                if symbol == target_symbol
                else portfolio.position(symbol)
            )
            if quantity == 0:
                continue
            price = target_price if symbol == target_symbol else portfolio.market_price(symbol)
            if price is None:
                raise ValueError(f"missing market price for risk calculation: {symbol}")
            signed_values.append(quantity * price)

        target_notional = abs(target_quantity * target_price)
        gross = sum(abs(value) for value in signed_values)
        absolute_net = abs(sum(signed_values))
        equity = portfolio.equity
        if equity <= 0.0:
            position_pct = None
            gross_pct = None
        else:
            position_pct = target_notional / equity
            gross_pct = gross / equity
        return _ExposureState(
            position_notional=target_notional,
            gross_exposure=gross,
            absolute_net_exposure=absolute_net,
            position_pct=position_pct,
            gross_exposure_pct=gross_pct,
        )

    @staticmethod
    def _reject(reason: RiskRejectReason, message: str) -> RiskDecision:
        return RiskDecision(approved=False, reason=reason, message=message)

