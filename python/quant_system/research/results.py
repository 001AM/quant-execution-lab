"""Typed outputs for parameter search and walk-forward research."""

from dataclasses import dataclass

import pandas as pd

from quant_system.backtest.engine import PerformanceValue
from quant_system.risk.report import RiskReport


@dataclass(frozen=True, slots=True)
class ParameterEvaluation:
    """One parameter combination and its in-sample objective score."""

    parameters: dict[str, object]
    score: float


@dataclass(frozen=True, slots=True)
class WalkForwardResult:
    """Frozen parameter choice and its out-of-sample result for one fold."""

    fold: int
    train_start: object
    train_end: object
    test_start: object
    test_end: object
    selected_parameters: dict[str, object]
    train_score: float
    test_performance: dict[str, PerformanceValue]
    test_returns: pd.Series
    test_equity: pd.Series
    number_of_fills: int


@dataclass(frozen=True, slots=True)
class WalkForwardSummary:
    """Sequential test-only results combined into one OOS research record."""

    folds: list[WalkForwardResult]
    combined_oos_returns: pd.Series
    combined_oos_equity: pd.Series
    performance: dict[str, PerformanceValue]
    risk_report: RiskReport
    total_oos_fills: int

    def to_text(self) -> str:
        """Format fold choices and aggregate OOS metrics for a research report."""

        def required_metric(metrics: dict[str, PerformanceValue], name: str) -> float:
            value = metrics[name]
            if value is None:
                raise ValueError(f"required report metric '{name}' is unavailable")
            return float(value)

        lines = ["WALK-FORWARD REPORT", "=" * 50]
        for fold in self.folds:
            lines.extend(
                [
                    f"Fold {fold.fold}",
                    f"Train: {fold.train_start} -> {fold.train_end}",
                    f"Test:  {fold.test_start} -> {fold.test_end}",
                    f"Selected: {fold.selected_parameters}",
                    f"Train score: {fold.train_score:.6f}",
                    f"OOS return: {required_metric(fold.test_performance, 'total_return'):.2%}",
                    "-" * 50,
                ]
            )
        total_return = required_metric(self.performance, "total_return")
        lines.extend(
            [
                "OVERALL OOS",
                f"Return: {total_return:.2%}",
                f"Fills: {self.total_oos_fills}",
                self.risk_report.to_text(),
            ]
        )
        return "\n".join(lines)
