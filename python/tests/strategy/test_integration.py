from collections.abc import Sequence
from datetime import datetime

import pandas as pd
import pytest

from quant_system.backtest import (
    BacktestEngine,
    build_backtest_report,
    buy_and_hold_return,
    compare_strategies,
)
from quant_system.execution import FixedCommission, PercentageSlippage
from quant_system.strategy import (
    MeanReversionStrategy,
    MomentumStrategy,
    MovingAverageCrossoverStrategy,
)


def price_data(prices: Sequence[float]) -> pd.DataFrame:
    closes = [float(price) for price in prices]
    return pd.DataFrame(
        {
            "timestamp": pd.date_range("2026-01-01", periods=len(closes), freq="D"),
            "symbol": "AAPL",
            "open": closes,
            "high": [price + 1.0 for price in closes],
            "low": [price - 1.0 for price in closes],
            "close": closes,
            "volume": 1_000.0,
        }
    )


def test_momentum_runs_through_engine_and_fills_next_bar() -> None:
    data = price_data([100.0, 100.0, 100.0, 105.0, 106.0])

    result = BacktestEngine(
        data,
        MomentumStrategy(lookback=3, threshold=0.02),
        quantity=100,
    ).run()

    assert len(result.signals) == 1
    assert result.signals[0].timestamp == datetime(2026, 1, 4)
    assert len(result.trades) == 1
    assert result.trades[0].timestamp == datetime(2026, 1, 5)
    assert result.trades[0].fill_price == 106.0


def test_mean_reversion_runs_through_same_engine() -> None:
    data = price_data([100.0, 100.0, 100.0, 100.0, 90.0, 100.0, 101.0])

    result = BacktestEngine(
        data,
        MeanReversionStrategy(window=5, entry_z=1.5, exit_z=0.5),
        quantity=100,
    ).run()

    assert [signal.side.value for signal in result.signals] == ["BUY", "EXIT"]
    assert [fill.fill_price for fill in result.trades] == [100.0, 101.0]


def test_moving_average_runs_through_same_engine() -> None:
    data = price_data([10.0, 10.0, 10.0, 9.0, 8.0, 9.0, 10.0, 11.0])

    result = BacktestEngine(
        data,
        MovingAverageCrossoverStrategy(fast_window=2, slow_window=3),
        quantity=100,
    ).run()

    assert len(result.signals) == 1
    assert result.signals[0].timestamp == datetime(2026, 1, 7)
    assert result.trades[0].timestamp == datetime(2026, 1, 8)
    assert result.trades[0].fill_price == 11.0


def test_transaction_costs_reduce_strategy_result() -> None:
    data = price_data([100.0, 100.0, 100.0, 105.0, 106.0, 100.0, 95.0, 94.0])
    free = BacktestEngine(
        data,
        MomentumStrategy(lookback=3, threshold=0.02),
        quantity=100,
    ).run()
    with_costs = BacktestEngine(
        data,
        MomentumStrategy(lookback=3, threshold=0.02),
        quantity=100,
        commission=FixedCommission(5.0),
        slippage=PercentageSlippage(0.001),
    ).run()

    assert len(free.trades) == 2
    assert with_costs.final_equity < free.final_equity


def test_strategy_comparison_uses_shared_engine_and_buy_hold_benchmark() -> None:
    data = price_data(
        [100.0, 100.0, 100.0, 95.0, 90.0, 95.0, 100.0, 105.0, 110.0, 105.0, 100.0]
    )
    strategies = {
        "momentum": MomentumStrategy(lookback=3, threshold=0.02),
        "mean_reversion": MeanReversionStrategy(window=5, entry_z=1.5, exit_z=0.5),
        "ma_crossover": MovingAverageCrossoverStrategy(fast_window=2, slow_window=4),
    }

    comparison = compare_strategies(
        data,
        strategies,
        quantity=100,
        allow_short_selling=True,
    )

    assert comparison["strategy"].tolist() == ["momentum", "mean_reversion", "ma_crossover"]
    # Independently calculated close-to-close benchmark: 100 / 100 - 1 = 0.
    assert comparison["benchmark_return"].tolist() == pytest.approx([0.0, 0.0, 0.0])
    assert (comparison["number_of_fills"] >= 1).all()
    assert (comparison["number_of_round_trips"] >= 0).all()


def test_buy_and_hold_benchmark_is_independently_close_to_close() -> None:
    data = price_data([100.0, 105.0, 110.0])

    assert buy_and_hold_return(data) == pytest.approx(0.10)


def test_backtest_report_exposes_metrics_and_defers_round_trip_count() -> None:
    data = price_data([100.0, 100.0, 100.0, 105.0, 106.0])
    result = BacktestEngine(
        data,
        MomentumStrategy(lookback=3, threshold=0.02),
        quantity=100,
    ).run()

    report = build_backtest_report(
        "momentum",
        result,
        benchmark_return=0.06,
    )

    assert report.strategy == "momentum"
    assert report.initial_capital == 100_000.0
    assert report.ending_equity == result.final_equity
    assert report.number_of_fills == 1
    assert report.number_of_round_trips == 0
    assert report.benchmark_return == 0.06


def test_production_strategy_is_deterministic_across_engine_runs() -> None:
    data = price_data([100.0, 100.0, 100.0, 105.0, 106.0, 100.0, 95.0, 94.0])
    engine = BacktestEngine(
        data,
        MomentumStrategy(lookback=3, threshold=0.02),
        quantity=100,
    )

    first = engine.run()
    second = engine.run()

    assert first.signals == second.signals
    assert first.orders == second.orders
    assert first.trades == second.trades
    assert first.performance == second.performance
    assert first.equity_curve.equals(second.equity_curve)
