"""Cash, position, P&L, exposure, and snapshot accounting."""

import math
from datetime import datetime

import pandas as pd

from quant_system.backtest.events import FillEvent, MarketEvent, OrderSide
from quant_system.portfolio.models import PortfolioSnapshot
from quant_system.portfolio.position import Position


class PortfolioError(ValueError):
    """Base class for portfolio accounting errors."""


class InsufficientCashError(PortfolioError):
    """Raised when a fill would produce negative cash."""


class ShortSellingDisabledError(PortfolioError):
    """Raised when a fill would create or increase a prohibited short."""


class DuplicateFillError(PortfolioError):
    """Raised when the same order fill is applied more than once."""


class MissingMarketPriceError(PortfolioError):
    """Raised when an open position has no current mark."""


class Portfolio:
    """Authoritative multi-symbol portfolio state.

    Position realized P&L is gross of commission. Commission is accumulated
    separately, and ``net_realized_pnl`` subtracts all commissions. Slippage is
    already reflected in fill prices and is never charged separately.
    """

    def __init__(self, initial_cash: float, *, allow_short_selling: bool = False) -> None:
        if isinstance(initial_cash, bool) or not isinstance(initial_cash, int | float):
            raise TypeError("initial_cash must be a real number")
        cash = float(initial_cash)
        if not math.isfinite(cash) or cash <= 0.0:
            raise ValueError("initial_cash must be finite and greater than zero")
        if not isinstance(allow_short_selling, bool):
            raise TypeError("allow_short_selling must be a bool")

        self.initial_cash = cash
        self.cash = cash
        self.allow_short_selling = allow_short_selling
        self.positions: dict[str, Position] = {}
        self._latest_prices: dict[str, float] = {}
        self._snapshots: list[PortfolioSnapshot] = []
        self._applied_order_ids: set[str] = set()
        self.total_commission = 0.0

    def position(self, symbol: str) -> int:
        position = self.positions.get(symbol)
        return 0 if position is None else position.quantity

    def get_position(self, symbol: str) -> Position | None:
        return self.positions.get(symbol)

    def can_process_fill(self, fill: FillEvent) -> bool:
        if fill.order_id in self._applied_order_ids:
            return False
        current_quantity = self.position(fill.symbol)
        quantity_change = fill.quantity if fill.side is OrderSide.BUY else -fill.quantity
        new_quantity = current_quantity + quantity_change
        if (
            not self.allow_short_selling
            and new_quantity < 0
            and abs(new_quantity) > abs(min(current_quantity, 0))
        ):
            return False
        notional = fill.quantity * fill.fill_price
        projected_cash = (
            self.cash - notional - fill.commission
            if fill.side is OrderSide.BUY
            else self.cash + notional - fill.commission
        )
        return projected_cash >= 0.0

    def process_fill(self, fill: FillEvent) -> None:
        """Apply cash and position accounting exactly once."""

        if fill.order_id in self._applied_order_ids:
            raise DuplicateFillError(f"fill for order '{fill.order_id}' was already processed")
        if not self.can_process_fill(fill):
            current_quantity = self.position(fill.symbol)
            quantity_change = fill.quantity if fill.side is OrderSide.BUY else -fill.quantity
            if not self.allow_short_selling and current_quantity + quantity_change < 0:
                raise ShortSellingDisabledError(
                    f"fill would create a short position in {fill.symbol}"
                )
            raise InsufficientCashError(
                f"insufficient cash for {fill.side.value} {fill.quantity} "
                f"{fill.symbol} at {fill.fill_price} plus commission {fill.commission}"
            )

        notional = fill.quantity * fill.fill_price
        if fill.side is OrderSide.BUY:
            self.cash -= notional + fill.commission
        else:
            self.cash += notional - fill.commission
        position = self.positions.setdefault(fill.symbol, Position(fill.symbol))
        position.apply_fill(fill.side, fill.quantity, fill.fill_price)
        self.total_commission += fill.commission
        self._applied_order_ids.add(fill.order_id)

    def update_market_price(self, symbol: str, price: float) -> None:
        if not isinstance(symbol, str) or not symbol.strip():
            raise ValueError("symbol must be a non-empty string")
        if isinstance(price, bool) or not isinstance(price, int | float):
            raise TypeError("price must be a real number")
        mark = float(price)
        if not math.isfinite(mark) or mark <= 0.0:
            raise ValueError("price must be finite and greater than zero")
        self._latest_prices[symbol.strip()] = mark

    def market_price(self, symbol: str) -> float | None:
        return self._latest_prices.get(symbol)

    def _mark_for(self, symbol: str) -> float:
        try:
            return self._latest_prices[symbol]
        except KeyError as exc:
            raise MissingMarketPriceError(f"missing market price for {symbol}") from exc

    @property
    def latest_prices(self) -> dict[str, float]:
        return dict(self._latest_prices)

    @property
    def gross_realized_pnl(self) -> float:
        return sum(position.realized_pnl for position in self.positions.values())

    @property
    def realized_pnl(self) -> float:
        """Alias for gross realized trading P&L, before commission."""

        return self.gross_realized_pnl

    @property
    def net_realized_pnl(self) -> float:
        return self.gross_realized_pnl - self.total_commission

    @property
    def total_round_trips(self) -> int:
        return sum(position.round_trips for position in self.positions.values())

    @property
    def unrealized_pnl(self) -> float:
        return sum(
            position.unrealized_pnl(self._mark_for(symbol))
            for symbol, position in self.positions.items()
            if not position.is_flat
        )

    @property
    def equity(self) -> float:
        return self.cash + self.net_exposure

    @property
    def gross_exposure(self) -> float:
        return sum(
            abs(position.market_value(self._mark_for(symbol)))
            for symbol, position in self.positions.items()
            if not position.is_flat
        )

    @property
    def net_exposure(self) -> float:
        return sum(
            position.market_value(self._mark_for(symbol))
            for symbol, position in self.positions.items()
            if not position.is_flat
        )

    @property
    def gross_exposure_pct(self) -> float:
        equity = self.equity
        if equity <= 0.0:
            raise PortfolioError("gross exposure percentage requires positive equity")
        return self.gross_exposure / equity

    @property
    def net_exposure_pct(self) -> float:
        equity = self.equity
        if equity <= 0.0:
            raise PortfolioError("net exposure percentage requires positive equity")
        return self.net_exposure / equity

    def record_snapshot(self, timestamp: datetime) -> PortfolioSnapshot:
        if not isinstance(timestamp, datetime):
            raise TypeError("timestamp must be a datetime")
        snapshot = PortfolioSnapshot(
            timestamp=timestamp,
            cash=self.cash,
            equity=self.equity,
            realized_pnl=self.gross_realized_pnl,
            unrealized_pnl=self.unrealized_pnl,
            gross_exposure=self.gross_exposure,
            net_exposure=self.net_exposure,
        )
        self._snapshots.append(snapshot)
        return snapshot

    def mark_to_market(self, event: MarketEvent) -> float:
        """Update a close mark and record a point-in-time portfolio snapshot."""

        self.update_market_price(event.symbol, event.close)
        return self.record_snapshot(event.timestamp).equity

    @property
    def snapshots(self) -> tuple[PortfolioSnapshot, ...]:
        return tuple(self._snapshots)

    @property
    def equity_curve(self) -> pd.Series:
        if not self._snapshots:
            return pd.Series([], dtype=float, name="equity")
        return pd.Series(
            [snapshot.equity for snapshot in self._snapshots],
            index=pd.DatetimeIndex(
                [snapshot.timestamp for snapshot in self._snapshots],
                name="timestamp",
            ),
            dtype=float,
            name="equity",
        )
