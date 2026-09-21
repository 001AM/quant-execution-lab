from datetime import datetime

import pandas as pd
import pytest

from quant_system.backtest import (
    BacktestEngine,
    BacktestResult,
    MarketEvent,
    SignalEvent,
    SignalSide,
)
from quant_system.execution import FixedCommission
from quant_system.research import (
    evaluate_parameter_grid,
    parameter_combinations,
    walk_forward_evaluate,
)
from quant_system.strategy import MomentumStrategy, Strategy


def price_data(prices: list[float]) -> pd.DataFrame:
    return pd.DataFrame(
        {
            "timestamp": pd.date_range("2026-01-01", periods=len(prices), freq="D"),
            "symbol": "AAPL",
            "open": prices,
            "high": [price + 1.0 for price in prices],
            "low": [price - 1.0 for price in prices],
            "close": prices,
            "volume": 1_000.0,
        }
    )


class FixedDirectionStrategy(Strategy):
    """Test-only strategy that enters once in a configured direction."""

    def __init__(self, direction: str) -> None:
        self.direction = direction
        self._emitted = False

    def reset(self) -> None:
        self._emitted = False

    def on_market(self, event: MarketEvent) -> list[SignalEvent]:
        if self._emitted:
            return []
        self._emitted = True
        side = SignalSide.BUY if self.direction == "buy" else SignalSide.SELL
        return [SignalEvent(event.timestamp, event.symbol, side)]


def fixed_direction_factory(**parameters: object) -> Strategy:
    return FixedDirectionStrategy(str(parameters["direction"]))


def leakage_data() -> pd.DataFrame:
    # Train rises (BUY wins); test falls (SELL would win).
    return price_data([100.0, 101.0, 102.0, 103.0, 104.0, 103.0, 102.0, 101.0])


def test_parameter_combinations_are_deterministic() -> None:
    combinations = parameter_combinations(
        {"lookback": [5, 10], "threshold": [0.01, 0.02]}
    )

    assert combinations == [
        {"lookback": 5, "threshold": 0.01},
        {"lookback": 5, "threshold": 0.02},
        {"lookback": 10, "threshold": 0.01},
        {"lookback": 10, "threshold": 0.02},
    ]


def test_grid_search_scores_candidates_on_training_data() -> None:
    evaluations = evaluate_parameter_grid(
        leakage_data().iloc[:4],
        fixed_direction_factory,
        {"direction": ["buy", "sell"]},
        objective="total_return",
        quantity=100,
        allow_short_selling=True,
    )

    assert evaluations[0].parameters == {"direction": "buy"}
    assert evaluations[0].score > evaluations[1].score


def test_explicit_leakage_case_selects_train_winner_not_test_winner() -> None:
    summary = walk_forward_evaluate(
        leakage_data(),
        fixed_direction_factory,
        {"direction": ["buy", "sell"]},
        train_size=4,
        test_size=4,
        objective="total_return",
        quantity=100,
        allow_short_selling=True,
    )

    assert summary.folds[0].selected_parameters == {"direction": "buy"}
    assert summary.folds[0].train_score > 0.0
    assert float(summary.folds[0].test_performance["total_return"]) < 0.0


def test_test_data_is_not_passed_to_training_objective() -> None:
    observed_lengths: list[int] = []

    def recording_objective(result: BacktestResult) -> float:
        observed_lengths.append(len(result.equity_curve))
        return result.total_return

    walk_forward_evaluate(
        leakage_data(),
        fixed_direction_factory,
        {"direction": ["buy", "sell"]},
        train_size=4,
        test_size=4,
        objective=recording_objective,
        quantity=100,
        allow_short_selling=True,
    )

    assert observed_lengths == [4, 4]


def test_warmup_history_can_signal_on_first_test_bar_without_train_trades() -> None:
    data = price_data([100.0, 100.0, 100.0, 105.0, 106.0])
    result = BacktestEngine(
        data.iloc[3:],
        MomentumStrategy(lookback=3, threshold=0.02),
        warmup_data=data.iloc[:3],
        quantity=100,
    ).run()

    assert [signal.timestamp for signal in result.signals] == [datetime(2026, 1, 4)]
    assert [fill.timestamp for fill in result.fills] == [datetime(2026, 1, 5)]
    assert result.equity_curve.index.min() == pd.Timestamp("2026-01-04")
    assert len(result.equity_curve) == 2


def test_walk_forward_applies_same_transaction_costs_to_oos() -> None:
    arguments = {
        "data": leakage_data(),
        "strategy_factory": fixed_direction_factory,
        "parameter_grid": {"direction": ["buy", "sell"]},
        "train_size": 4,
        "test_size": 4,
        "objective": "total_return",
        "quantity": 100,
        "allow_short_selling": True,
    }
    free = walk_forward_evaluate(**arguments)
    costly = walk_forward_evaluate(**arguments, commission=FixedCommission(5.0))

    assert costly.combined_oos_equity.iloc[-1] < free.combined_oos_equity.iloc[-1]


def test_combined_oos_curve_contains_only_test_periods() -> None:
    data = leakage_data()
    summary = walk_forward_evaluate(
        data,
        fixed_direction_factory,
        {"direction": ["buy", "sell"]},
        train_size=4,
        test_size=4,
        objective="total_return",
        quantity=100,
        allow_short_selling=True,
    )

    assert summary.combined_oos_returns.index.tolist() == data["timestamp"].iloc[4:].tolist()
    assert len(summary.combined_oos_equity) == 4
    assert summary.total_oos_fills == 1


def test_walk_forward_is_deterministic() -> None:
    arguments = {
        "data": leakage_data(),
        "strategy_factory": fixed_direction_factory,
        "parameter_grid": {"direction": ["buy", "sell"]},
        "train_size": 4,
        "test_size": 4,
        "objective": "total_return",
        "quantity": 100,
        "allow_short_selling": True,
    }

    first = walk_forward_evaluate(**arguments)
    second = walk_forward_evaluate(**arguments)

    assert first.folds[0].selected_parameters == second.folds[0].selected_parameters
    assert first.performance == second.performance
    assert first.combined_oos_returns.equals(second.combined_oos_returns)


def test_overlapping_oos_folds_are_rejected() -> None:
    with pytest.raises(ValueError, match="non-overlapping"):
        walk_forward_evaluate(
            price_data([100.0] * 10),
            fixed_direction_factory,
            {"direction": ["buy"]},
            train_size=4,
            test_size=3,
            step_size=2,
            objective="total_return",
        )
