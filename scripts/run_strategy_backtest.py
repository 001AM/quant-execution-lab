"""Run a lightweight strategy comparison on a canonical OHLCV CSV."""

import argparse
from collections.abc import Sequence

from quant_system.backtest import compare_strategies
from quant_system.data import MarketDataLoader
from quant_system.execution import PercentageCommission, PercentageSlippage
from quant_system.strategy import (
    MeanReversionStrategy,
    MomentumStrategy,
    MovingAverageCrossoverStrategy,
    Strategy,
)


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", default="data/sample/AAPL.csv")
    parser.add_argument("--symbol", default="AAPL")
    parser.add_argument(
        "--strategy",
        choices=("all", "momentum", "mean-reversion", "ma-crossover"),
        default="all",
    )
    return parser.parse_args(argv)


def configured_strategies(selection: str) -> dict[str, Strategy]:
    available: dict[str, Strategy] = {
        "momentum": MomentumStrategy(lookback=5, threshold=0.01),
        "mean_reversion": MeanReversionStrategy(window=5, entry_z=1.5, exit_z=0.5),
        "ma_crossover": MovingAverageCrossoverStrategy(fast_window=3, slow_window=7),
    }
    if selection == "all":
        return available
    key = selection.replace("-", "_")
    return {key: available[key]}


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    data = MarketDataLoader().load_csv(args.data, symbol=args.symbol)
    comparison = compare_strategies(
        data,
        configured_strategies(args.strategy),
        commission=PercentageCommission(0.001),
        slippage=PercentageSlippage(0.0005),
        allow_short_selling=True,
    )
    print(comparison.to_string(index=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

