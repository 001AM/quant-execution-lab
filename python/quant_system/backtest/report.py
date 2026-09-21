"""Research reports and side-by-side strategy comparison."""

from collections.abc import Mapping
from dataclasses import dataclass

import pandas as pd

from quant_system.backtest.engine import BacktestEngine, BacktestResult
from quant_system.data.models import CLOSE, SYMBOL
from quant_system.execution.costs import CommissionModel
from quant_system.execution.slippage import SlippageModel
from quant_system.strategy.base import Strategy


@dataclass(frozen=True, slots=True)
class BacktestReport:
    """Comparable summary built from a BacktestResult and Set 2 metrics."""

    strategy: str
    initial_capital: float
    ending_equity: float
    total_return: float
    benchmark_return: float
    cagr: float | None
    annualized_volatility: float | None
    sharpe_ratio: float | None
    sortino_ratio: float | None
    max_drawdown: float
    number_of_fills: int
    number_of_round_trips: int

    def to_dict(self) -> dict[str, str | float | int | None]:
        return {
            "strategy": self.strategy,
            "initial_capital": self.initial_capital,
            "ending_equity": self.ending_equity,
            "total_return": self.total_return,
            "benchmark_return": self.benchmark_return,
            "cagr": self.cagr,
            "annualized_volatility": self.annualized_volatility,
            "sharpe_ratio": self.sharpe_ratio,
            "sortino_ratio": self.sortino_ratio,
            "max_drawdown": self.max_drawdown,
            "number_of_fills": self.number_of_fills,
            "number_of_round_trips": self.number_of_round_trips,
        }


def _optional_float(value: float | int | None) -> float | None:
    return None if value is None else float(value)


def buy_and_hold_return(data: pd.DataFrame) -> float:
    """Calculate close-to-close buy-and-hold return for one symbol."""

    if data.empty:
        raise ValueError("benchmark data must not be empty")
    if SYMBOL not in data or CLOSE not in data:
        raise ValueError("benchmark data requires symbol and close columns")
    symbols = data[SYMBOL].dropna().unique()
    if len(symbols) != 1:
        raise ValueError("buy-and-hold benchmark requires exactly one symbol")
    first_close = float(data[CLOSE].iloc[0])
    last_close = float(data[CLOSE].iloc[-1])
    if first_close <= 0.0:
        raise ValueError("benchmark starting close must be greater than zero")
    return last_close / first_close - 1.0


def build_backtest_report(
    strategy_name: str,
    result: BacktestResult,
    *,
    benchmark_return: float,
) -> BacktestReport:
    """Build a report without duplicating any performance formula."""

    performance = result.performance
    maximum_drawdown = performance["max_drawdown"]
    if maximum_drawdown is None:
        raise ValueError("backtest result does not contain maximum drawdown")
    return BacktestReport(
        strategy=strategy_name,
        initial_capital=result.initial_capital,
        ending_equity=result.final_equity,
        total_return=result.total_return,
        benchmark_return=benchmark_return,
        cagr=_optional_float(performance["cagr"]),
        annualized_volatility=_optional_float(performance["annualized_volatility"]),
        sharpe_ratio=_optional_float(performance["sharpe_ratio"]),
        sortino_ratio=_optional_float(performance["sortino_ratio"]),
        max_drawdown=float(maximum_drawdown),
        number_of_fills=len(result.trades),
        number_of_round_trips=result.portfolio.total_round_trips,
    )


def compare_strategies(
    data: pd.DataFrame,
    strategies: Mapping[str, Strategy],
    *,
    initial_capital: float = 100_000.0,
    quantity: int = 100,
    commission: CommissionModel | None = None,
    slippage: SlippageModel | None = None,
    risk_free_rate: float = 0.0,
    periods_per_year: int = 252,
    allow_short_selling: bool = False,
) -> pd.DataFrame:
    """Run interchangeable strategies through identical engine configuration."""

    if not strategies:
        raise ValueError("at least one strategy is required")
    benchmark = buy_and_hold_return(data)
    reports: list[dict[str, str | float | int | None]] = []
    for name, strategy in strategies.items():
        result = BacktestEngine(
            data=data,
            strategy=strategy,
            initial_capital=initial_capital,
            quantity=quantity,
            commission=commission,
            slippage=slippage,
            risk_free_rate=risk_free_rate,
            periods_per_year=periods_per_year,
            allow_short_selling=allow_short_selling,
        ).run()
        reports.append(
            build_backtest_report(
                name,
                result,
                benchmark_return=benchmark,
            ).to_dict()
        )
    return pd.DataFrame(reports)
