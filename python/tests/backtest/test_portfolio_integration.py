from datetime import datetime

import pandas as pd
import pytest

from quant_system.backtest import BacktestEngine, SignalEvent, SignalSide
from quant_system.risk import RiskLimits, RiskManager, RiskRejectReason
from quant_system.strategy import ScheduledSignalStrategy


def round_trip_data() -> pd.DataFrame:
    opens = [100.0, 101.0, 110.0, 111.0]
    closes = [100.0, 102.0, 110.0, 112.0]
    return pd.DataFrame(
        {
            "timestamp": pd.date_range("2026-01-01", periods=4, freq="D"),
            "symbol": "AAPL",
            "open": opens,
            "high": [
                max(open_price, close) + 1.0
                for open_price, close in zip(opens, closes, strict=True)
            ],
            "low": [
                min(open_price, close) - 1.0
                for open_price, close in zip(opens, closes, strict=True)
            ],
            "close": closes,
            "volume": 1_000.0,
        }
    )


def round_trip_strategy() -> ScheduledSignalStrategy:
    return ScheduledSignalStrategy(
        [
            SignalEvent(datetime(2026, 1, 1), "AAPL", SignalSide.BUY),
            SignalEvent(datetime(2026, 1, 3), "AAPL", SignalSide.EXIT),
        ]
    )


def test_approved_order_fills_next_bar_and_updates_position_pnl() -> None:
    result = BacktestEngine(
        round_trip_data(),
        round_trip_strategy(),
        quantity=100,
        risk_manager=RiskManager(RiskLimits(max_position_notional=20_000.0)),
    ).run()

    assert result.rejected_orders == []
    assert [fill.timestamp for fill in result.fills] == [
        datetime(2026, 1, 2),
        datetime(2026, 1, 4),
    ]
    assert [fill.fill_price for fill in result.fills] == [101.0, 111.0]
    position = result.portfolio.get_position("AAPL")
    assert position is not None
    assert position.is_flat
    assert position.realized_pnl == pytest.approx(1_000.0)
    assert result.portfolio.gross_realized_pnl == pytest.approx(1_000.0)
    assert result.portfolio.total_round_trips == 1
    assert result.portfolio.unrealized_pnl == 0.0
    assert result.final_equity == pytest.approx(101_000.0)


def test_engine_records_hand_calculated_portfolio_snapshots() -> None:
    result = BacktestEngine(
        round_trip_data(),
        round_trip_strategy(),
        quantity=100,
    ).run()

    assert len(result.portfolio_snapshots) == 4
    assert [snapshot.equity for snapshot in result.portfolio_snapshots] == pytest.approx(
        [100_000.0, 100_100.0, 100_900.0, 101_000.0]
    )
    assert result.portfolio_snapshots[1].unrealized_pnl == pytest.approx(100.0)
    assert result.portfolio_snapshots[2].unrealized_pnl == pytest.approx(900.0)
    assert result.portfolio_snapshots[-1].realized_pnl == pytest.approx(1_000.0)
    assert result.equity_curve.tolist() == pytest.approx(
        [100_000.0, 100_100.0, 100_900.0, 101_000.0]
    )


def test_risk_rejection_is_recorded_and_never_reaches_execution() -> None:
    result = BacktestEngine(
        round_trip_data(),
        round_trip_strategy(),
        quantity=100,
        risk_manager=RiskManager(RiskLimits(max_position_notional=5_000.0)),
    ).run()

    assert len(result.orders) == 1
    assert result.fills == []
    assert result.pending_orders == []
    assert len(result.rejected_orders) == 1
    assert result.rejected_orders[0].reason is RiskRejectReason.MAX_POSITION_NOTIONAL
    assert result.portfolio.position("AAPL") == 0
    assert result.final_equity == 100_000.0


def test_portfolio_engine_run_is_deterministic() -> None:
    engine = BacktestEngine(
        round_trip_data(),
        round_trip_strategy(),
        quantity=100,
        risk_manager=RiskManager(RiskLimits(max_gross_exposure=20_000.0)),
    )

    first = engine.run()
    second = engine.run()

    assert first.fills == second.fills
    assert first.rejected_orders == second.rejected_orders
    assert first.portfolio_snapshots == second.portfolio_snapshots
    assert first.equity_curve.equals(second.equity_curve)
    assert first.portfolio.gross_realized_pnl == second.portfolio.gross_realized_pnl
