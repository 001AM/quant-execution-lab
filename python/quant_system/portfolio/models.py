"""Immutable portfolio output models."""

from dataclasses import dataclass
from datetime import datetime


@dataclass(frozen=True, slots=True)
class PortfolioSnapshot:
    timestamp: datetime
    cash: float
    equity: float
    realized_pnl: float
    unrealized_pnl: float
    gross_exposure: float
    net_exposure: float

