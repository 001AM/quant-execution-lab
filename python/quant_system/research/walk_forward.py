"""Leakage-resistant parameter search and walk-forward evaluation."""

import itertools
import math
from collections.abc import Callable, Mapping, Sequence
from typing import Literal

import pandas as pd

from quant_system.backtest.engine import BacktestEngine, BacktestResult, PerformanceValue
from quant_system.execution.costs import CommissionModel
from quant_system.execution.slippage import SlippageModel
from quant_system.quant.metrics import (
    annualized_volatility,
    cagr,
    max_drawdown,
    sharpe_ratio,
    sortino_ratio,
)
from quant_system.research.results import (
    ParameterEvaluation,
    WalkForwardResult,
    WalkForwardSummary,
)
from quant_system.research.split import walk_forward_splits
from quant_system.risk.manager import RiskManager
from quant_system.risk.report import generate_risk_report
from quant_system.strategy.base import Strategy

type StrategyFactory = Callable[..., Strategy]
type Objective = Literal["sharpe_ratio", "total_return"] | Callable[[BacktestResult], float]


def parameter_combinations(
    parameter_grid: Mapping[str, Sequence[object]],
) -> list[dict[str, object]]:
    """Expand a small parameter grid in stable insertion/product order."""

    if not isinstance(parameter_grid, Mapping):
        raise TypeError("parameter_grid must be a mapping")
    if not parameter_grid:
        raise ValueError("parameter_grid must not be empty")

    names: list[str] = []
    choices: list[Sequence[object]] = []
    for name, values in parameter_grid.items():
        if not isinstance(name, str) or not name:
            raise ValueError("parameter names must be non-empty strings")
        if isinstance(values, str) or not isinstance(values, Sequence):
            raise TypeError(f"parameter '{name}' values must be a sequence")
        if not values:
            raise ValueError(f"parameter '{name}' must have at least one candidate")
        names.append(name)
        choices.append(values)
    return [
        dict(zip(names, combination, strict=True))
        for combination in itertools.product(*choices)
    ]


def _objective_score(result: BacktestResult, objective: Objective) -> float:
    if callable(objective):
        raw_score: float | int | None = objective(result)
    elif objective in {"sharpe_ratio", "total_return"}:
        raw_score = result.performance[objective]
        if raw_score is None:
            return -math.inf
    else:
        raise ValueError("objective must be 'sharpe_ratio', 'total_return', or a callable")
    if isinstance(raw_score, bool) or not isinstance(raw_score, int | float):
        raise TypeError("objective must return a real number")
    value = float(raw_score)
    return value if math.isfinite(value) else -math.inf


def _run_backtest(
    data: pd.DataFrame,
    strategy: Strategy,
    *,
    initial_capital: float,
    quantity: int,
    commission: CommissionModel | None,
    slippage: SlippageModel | None,
    risk_free_rate: float,
    periods_per_year: int,
    allow_short_selling: bool,
    risk_manager: RiskManager | None,
    warmup_data: pd.DataFrame | None = None,
) -> BacktestResult:
    return BacktestEngine(
        data,
        strategy,
        initial_capital=initial_capital,
        quantity=quantity,
        commission=commission,
        slippage=slippage,
        risk_free_rate=risk_free_rate,
        periods_per_year=periods_per_year,
        allow_short_selling=allow_short_selling,
        risk_manager=risk_manager,
        warmup_data=warmup_data,
    ).run()


def evaluate_parameter_grid(
    train_data: pd.DataFrame,
    strategy_factory: StrategyFactory,
    parameter_grid: Mapping[str, Sequence[object]],
    *,
    objective: Objective = "sharpe_ratio",
    initial_capital: float = 100_000.0,
    quantity: int = 100,
    commission: CommissionModel | None = None,
    slippage: SlippageModel | None = None,
    risk_free_rate: float = 0.0,
    periods_per_year: int = 252,
    allow_short_selling: bool = False,
    risk_manager: RiskManager | None = None,
) -> list[ParameterEvaluation]:
    """Score every candidate using training data only."""

    evaluations: list[ParameterEvaluation] = []
    for parameters in parameter_combinations(parameter_grid):
        result = _run_backtest(
            train_data,
            strategy_factory(**parameters),
            initial_capital=initial_capital,
            quantity=quantity,
            commission=commission,
            slippage=slippage,
            risk_free_rate=risk_free_rate,
            periods_per_year=periods_per_year,
            allow_short_selling=allow_short_selling,
            risk_manager=risk_manager,
        )
        evaluations.append(
            ParameterEvaluation(
                parameters=dict(parameters),
                score=_objective_score(result, objective),
            )
        )
    return evaluations


def _returns_from_result(result: BacktestResult) -> pd.Series:
    previous = pd.concat(
        [
            pd.Series([result.initial_capital], dtype=float),
            result.equity_curve.reset_index(drop=True),
        ],
        ignore_index=True,
    )
    values = previous.pct_change(fill_method=None).iloc[1:].to_numpy(dtype=float)
    return pd.Series(values, index=result.equity_curve.index, dtype=float, name="return")


def _equity_with_baseline(equity: pd.Series, initial_capital: float) -> pd.Series:
    return pd.concat(
        [pd.Series([initial_capital], dtype=float), equity.reset_index(drop=True)],
        ignore_index=True,
    )


def _combined_performance(
    returns: pd.Series,
    equity: pd.Series,
    *,
    initial_capital: float,
    risk_free_rate: float,
    periods_per_year: int,
) -> dict[str, PerformanceValue]:
    with_baseline = _equity_with_baseline(equity, initial_capital)

    def optional(metric: Callable[[], float]) -> float | None:
        try:
            return metric()
        except ValueError:
            return None

    return {
        "total_return": float(equity.iloc[-1] / initial_capital - 1.0),
        "cagr": optional(lambda: cagr(with_baseline, periods_per_year)),
        "annualized_volatility": optional(
            lambda: annualized_volatility(returns, periods_per_year)
        ),
        "sharpe_ratio": optional(
            lambda: sharpe_ratio(returns, risk_free_rate, periods_per_year)
        ),
        "sortino_ratio": optional(
            lambda: sortino_ratio(returns, risk_free_rate, periods_per_year)
        ),
        "max_drawdown": max_drawdown(with_baseline),
        "ending_equity": float(equity.iloc[-1]),
        "observations": len(returns),
    }


def walk_forward_evaluate(
    data: pd.DataFrame,
    strategy_factory: StrategyFactory,
    parameter_grid: Mapping[str, Sequence[object]],
    *,
    train_size: int,
    test_size: int,
    step_size: int | None = None,
    expanding: bool = False,
    objective: Objective = "sharpe_ratio",
    initial_capital: float = 100_000.0,
    quantity: int = 100,
    commission: CommissionModel | None = None,
    slippage: SlippageModel | None = None,
    risk_free_rate: float = 0.0,
    periods_per_year: int = 252,
    allow_short_selling: bool = False,
    risk_manager: RiskManager | None = None,
) -> WalkForwardSummary:
    """Select on each train fold, freeze parameters, then evaluate its test fold.

    Training bars are passed to the test strategy only through the non-trading
    warm-up channel. They never contribute test fills, returns, or equity.
    """

    effective_step = test_size if step_size is None else step_size
    if effective_step < test_size:
        raise ValueError("walk-forward evaluation requires non-overlapping test folds")
    splits = walk_forward_splits(
        data,
        train_size,
        test_size,
        step_size,
        expanding=expanding,
    )
    fold_results: list[WalkForwardResult] = []
    return_parts: list[pd.Series] = []
    total_fills = 0

    for split in splits:
        evaluations = evaluate_parameter_grid(
            split.train_data,
            strategy_factory,
            parameter_grid,
            objective=objective,
            initial_capital=initial_capital,
            quantity=quantity,
            commission=commission,
            slippage=slippage,
            risk_free_rate=risk_free_rate,
            periods_per_year=periods_per_year,
            allow_short_selling=allow_short_selling,
            risk_manager=risk_manager,
        )
        selected = max(evaluations, key=lambda evaluation: evaluation.score)
        test_result = _run_backtest(
            split.test_data,
            strategy_factory(**selected.parameters),
            initial_capital=initial_capital,
            quantity=quantity,
            commission=commission,
            slippage=slippage,
            risk_free_rate=risk_free_rate,
            periods_per_year=periods_per_year,
            allow_short_selling=allow_short_selling,
            risk_manager=risk_manager,
            warmup_data=split.train_data,
        )
        test_returns = _returns_from_result(test_result)
        return_parts.append(test_returns)
        total_fills += len(test_result.fills)
        fold_results.append(
            WalkForwardResult(
                fold=split.fold,
                train_start=split.train_start,
                train_end=split.train_end,
                test_start=split.test_start,
                test_end=split.test_end,
                selected_parameters=dict(selected.parameters),
                train_score=selected.score,
                test_performance=dict(test_result.performance),
                test_returns=test_returns,
                test_equity=test_result.equity_curve.copy(),
                number_of_fills=len(test_result.fills),
            )
        )

    combined_returns = pd.concat(return_parts)
    if len(combined_returns) < 2:
        raise ValueError("walk-forward evaluation requires at least two OOS observations")
    combined_equity = initial_capital * (1.0 + combined_returns).cumprod()
    combined_equity.name = "equity"
    performance = _combined_performance(
        combined_returns,
        combined_equity,
        initial_capital=initial_capital,
        risk_free_rate=risk_free_rate,
        periods_per_year=periods_per_year,
    )
    risk_equity = _equity_with_baseline(combined_equity, initial_capital)
    risk_report = generate_risk_report(
        combined_returns,
        risk_equity,
        risk_free_rate=risk_free_rate,
        periods_per_year=periods_per_year,
    )
    return WalkForwardSummary(
        folds=fold_results,
        combined_oos_returns=combined_returns,
        combined_oos_equity=combined_equity,
        performance=performance,
        risk_report=risk_report,
        total_oos_fills=total_fills,
    )
