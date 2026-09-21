"""Deterministic event-driven backtesting."""

from typing import TYPE_CHECKING

from quant_system.backtest.events import (
    Event,
    EventQueue,
    EventType,
    FillEvent,
    MarketEvent,
    OrderEvent,
    OrderSide,
    OrderType,
    SignalEvent,
    SignalSide,
)
from quant_system.backtest.orders import FixedSizeOrderGenerator

if TYPE_CHECKING:
    from quant_system.backtest.engine import BacktestEngine, BacktestResult
    from quant_system.backtest.report import BacktestReport

__all__ = [
    "BacktestEngine",
    "BacktestReport",
    "BacktestResult",
    "Event",
    "EventQueue",
    "EventType",
    "FillEvent",
    "FixedSizeOrderGenerator",
    "MarketEvent",
    "OrderEvent",
    "OrderSide",
    "OrderType",
    "SignalEvent",
    "SignalSide",
    "build_backtest_report",
    "buy_and_hold_return",
    "compare_strategies",
]


def __getattr__(name: str) -> object:
    """Load engine exports lazily to keep event imports dependency-neutral."""

    if name in {"BacktestEngine", "BacktestResult"}:
        from quant_system.backtest.engine import BacktestEngine, BacktestResult

        return {"BacktestEngine": BacktestEngine, "BacktestResult": BacktestResult}[name]
    if name in {
        "BacktestReport",
        "build_backtest_report",
        "buy_and_hold_return",
        "compare_strategies",
    }:
        from quant_system.backtest.report import (
            BacktestReport,
            build_backtest_report,
            buy_and_hold_return,
            compare_strategies,
        )

        return {
            "BacktestReport": BacktestReport,
            "build_backtest_report": build_backtest_report,
            "buy_and_hold_return": buy_and_hold_return,
            "compare_strategies": compare_strategies,
        }[name]
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
