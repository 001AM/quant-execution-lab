"""Compatibility adapter for the Set 3 quantity-only Account API."""

import pandas as pd

from quant_system.backtest.events import FillEvent, MarketEvent
from quant_system.portfolio.portfolio import InsufficientCashError, Portfolio


class Account:
    """Deprecated quantity view backed by the full :class:`Portfolio`."""

    def __init__(self, initial_cash: float, *, allow_short_selling: bool = False) -> None:
        self._portfolio = Portfolio(
            initial_cash,
            allow_short_selling=allow_short_selling,
        )

    @property
    def initial_cash(self) -> float:
        return self._portfolio.initial_cash

    @property
    def cash(self) -> float:
        return self._portfolio.cash

    @property
    def allow_short_selling(self) -> bool:
        return self._portfolio.allow_short_selling

    @property
    def positions(self) -> dict[str, int]:
        return {
            symbol: position.quantity
            for symbol, position in self._portfolio.positions.items()
            if not position.is_flat
        }

    def position(self, symbol: str) -> int:
        return self._portfolio.position(symbol)

    def can_apply_fill(self, fill: FillEvent) -> bool:
        return self._portfolio.can_process_fill(fill)

    def apply_fill(self, fill: FillEvent) -> None:
        if not self.can_apply_fill(fill):
            raise InsufficientCashError(
                "fill rejected by cash/short-selling constraints: "
                f"{fill.side.value} {fill.quantity} {fill.symbol}"
            )
        self._portfolio.process_fill(fill)

    def mark_to_market(self, event: MarketEvent) -> float:
        return self._portfolio.mark_to_market(event)

    @property
    def latest_prices(self) -> dict[str, float]:
        return self._portfolio.latest_prices

    @property
    def equity_curve(self) -> pd.Series:
        return self._portfolio.equity_curve
