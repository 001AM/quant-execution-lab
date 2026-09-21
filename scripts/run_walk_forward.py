"""Run a deterministic cost-aware momentum walk-forward study."""

import argparse
from collections.abc import Sequence

from quant_system.data import MarketDataLoader
from quant_system.execution import PercentageCommission, PercentageSlippage
from quant_system.research import walk_forward_evaluate
from quant_system.strategy import MomentumStrategy, Strategy


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", default="data/sample/AAPL.csv")
    parser.add_argument("--symbol", default="AAPL")
    return parser.parse_args(argv)


def momentum_factory(lookback: int, threshold: float) -> Strategy:
    return MomentumStrategy(lookback=lookback, threshold=threshold)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    data = MarketDataLoader().load_csv(args.data, symbol=args.symbol)
    summary = walk_forward_evaluate(
        data,
        momentum_factory,
        {
            "lookback": [3, 5, 8],
            "threshold": [0.005, 0.01],
        },
        train_size=15,
        test_size=5,
        step_size=5,
        objective="total_return",
        quantity=100,
        commission=PercentageCommission(0.001),
        slippage=PercentageSlippage(0.0005),
        allow_short_selling=True,
    )
    print(summary.to_text())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
