from datetime import datetime

import pandas as pd
import pytest

from quant_system.backtest import (
    BacktestEngine,
    FillEvent,
    MarketEvent,
    OrderEvent,
    OrderSide,
    SignalEvent,
    SignalSide,
)
from quant_system.execution import FixedCommission, PercentageSlippage
from quant_system.strategy import ScheduledSignalStrategy


def six_bar_data() -> pd.DataFrame:
    timestamps = pd.to_datetime(
        [
            "2026-01-01",
            "2026-01-02",
            "2026-01-03",
            "2026-01-04",
            "2026-01-05",
            "2026-01-06",
        ]
    )
    opens = [100.0, 102.0, 104.0, 106.0, 108.0, 110.0]
    closes = [101.0, 103.0, 105.0, 107.0, 109.0, 111.0]
    return pd.DataFrame(
        {
            "timestamp": timestamps,
            "symbol": "AAPL",
            "open": opens,
            "high": [close + 1.0 for close in closes],
            "low": [open_price - 1.0 for open_price in opens],
            "close": closes,
            "volume": 1_000.0,
        }
    )


def timestamp(day: int) -> datetime:
    return datetime(2026, 1, day)


def round_trip_strategy() -> ScheduledSignalStrategy:
    return ScheduledSignalStrategy(
        [
            SignalEvent(timestamp(2), "AAPL", SignalSide.BUY),
            SignalEvent(timestamp(5), "AAPL", SignalSide.EXIT),
        ]
    )


def test_complete_pipeline_and_no_look_ahead() -> None:
    result = BacktestEngine(
        six_bar_data(),
        round_trip_strategy(),
        quantity=100,
        initial_capital=100_000.0,
    ).run()

    assert [trade.side for trade in result.trades] == [OrderSide.BUY, OrderSide.SELL]
    assert [trade.timestamp for trade in result.trades] == [timestamp(3), timestamp(6)]
    assert [trade.fill_price for trade in result.trades] == [104.0, 110.0]
    assert result.final_equity == 100_600.0
    assert result.total_return == pytest.approx(0.006)
    assert result.performance["ending_equity"] == 100_600.0
    assert result.performance["total_return"] == pytest.approx(0.006)


def test_event_trace_contains_each_pipeline_stage_in_order() -> None:
    result = BacktestEngine(six_bar_data(), round_trip_strategy(), quantity=100).run()

    buy_signal_index = next(
        index
        for index, event in enumerate(result.events)
        if isinstance(event, SignalEvent) and event.side is SignalSide.BUY
    )
    buy_order_index = next(
        index
        for index, event in enumerate(result.events)
        if isinstance(event, OrderEvent) and event.side is OrderSide.BUY
    )
    buy_fill_index = next(
        index
        for index, event in enumerate(result.events)
        if isinstance(event, FillEvent) and event.side is OrderSide.BUY
    )
    day_three_market_index = next(
        index
        for index, event in enumerate(result.events)
        if isinstance(event, MarketEvent) and event.timestamp == timestamp(3)
    )

    assert buy_signal_index < buy_order_index < day_three_market_index < buy_fill_index


def test_no_trade_strategy_preserves_capital() -> None:
    result = BacktestEngine(
        six_bar_data(),
        ScheduledSignalStrategy([]),
        initial_capital=100_000.0,
    ).run()

    assert result.trades == []
    assert result.orders == []
    assert result.final_equity == 100_000.0
    assert result.performance["max_drawdown"] == 0.0
    assert result.performance["sharpe_ratio"] is None


def test_final_bar_order_remains_pending() -> None:
    strategy = ScheduledSignalStrategy(
        [SignalEvent(timestamp(6), "AAPL", SignalSide.BUY)]
    )

    result = BacktestEngine(
        six_bar_data(),
        strategy,
        quantity=100,
    ).run()

    assert result.trades == []
    assert len(result.orders) == 1
    assert result.pending_orders == result.orders
    assert result.pending_orders[0].timestamp == timestamp(6)


def test_transaction_costs_reduce_performance() -> None:
    free_result = BacktestEngine(
        six_bar_data(),
        round_trip_strategy(),
        quantity=100,
    ).run()
    cost_result = BacktestEngine(
        six_bar_data(),
        round_trip_strategy(),
        quantity=100,
        commission=FixedCommission(5.0),
        slippage=PercentageSlippage(0.001),
    ).run()

    assert cost_result.final_equity == pytest.approx(100_568.60)
    assert cost_result.final_equity < free_result.final_equity
    assert sum(fill.commission for fill in cost_result.trades) == 10.0
    assert sum(fill.slippage for fill in cost_result.trades) == pytest.approx(21.4)


def test_sell_then_exit_covers_short_on_next_bar() -> None:
    strategy = ScheduledSignalStrategy(
        [
            SignalEvent(timestamp(2), "AAPL", SignalSide.SELL),
            SignalEvent(timestamp(5), "AAPL", SignalSide.EXIT),
        ]
    )

    result = BacktestEngine(
        six_bar_data(),
        strategy,
        quantity=100,
        allow_short_selling=True,
    ).run()

    assert [trade.side for trade in result.trades] == [OrderSide.SELL, OrderSide.BUY]
    assert [trade.fill_price for trade in result.trades] == [104.0, 110.0]
    assert result.final_equity == 99_400.0


def test_insufficient_cash_rejects_fill_and_preserves_account() -> None:
    strategy = ScheduledSignalStrategy(
        [SignalEvent(timestamp(2), "AAPL", SignalSide.BUY)]
    )

    result = BacktestEngine(
        six_bar_data(),
        strategy,
        initial_capital=1_000.0,
        quantity=100,
    ).run()

    assert result.trades == []
    assert len(result.rejected_orders) == 1
    assert result.pending_orders == []
    assert result.final_equity == 1_000.0


def test_multiple_symbols_fill_on_their_own_next_bars() -> None:
    data = pd.DataFrame(
        {
            "timestamp": pd.to_datetime(
                ["2026-01-01", "2026-01-01", "2026-01-02", "2026-01-02"]
            ),
            "symbol": ["AAPL", "MSFT", "AAPL", "MSFT"],
            "open": [100.0, 200.0, 102.0, 202.0],
            "high": [102.0, 202.0, 104.0, 204.0],
            "low": [99.0, 199.0, 101.0, 201.0],
            "close": [101.0, 201.0, 103.0, 203.0],
            "volume": [1_000.0] * 4,
        }
    )
    strategy = ScheduledSignalStrategy(
        [
            SignalEvent(timestamp(1), "AAPL", SignalSide.BUY),
            SignalEvent(timestamp(1), "MSFT", SignalSide.SELL),
        ]
    )

    result = BacktestEngine(
        data,
        strategy,
        quantity=10,
        allow_short_selling=True,
    ).run()

    assert [(fill.symbol, fill.fill_price) for fill in result.trades] == [
        ("AAPL", 102.0),
        ("MSFT", 202.0),
    ]


def test_repeated_runs_are_deterministic() -> None:
    engine = BacktestEngine(six_bar_data(), round_trip_strategy(), quantity=100)

    first = engine.run()
    second = engine.run()

    assert first.orders == second.orders
    assert first.trades == second.trades
    assert first.pending_orders == second.pending_orders
    assert first.performance == second.performance
    assert first.equity_curve.equals(second.equity_curve)
