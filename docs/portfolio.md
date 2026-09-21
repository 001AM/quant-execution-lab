# Quant Execution Lab — Five-Minute Portfolio Guide

## What this repository demonstrates

This is a deterministic hybrid Python/C++ trading-system laboratory. It follows an intended order
from validated data, through signal research and portfolio accounting, into native execution and
measured performance. It is not presented as a brokerage, exchange, or guaranteed-profit system.

```text
Python: data → strategies → event backtest → portfolio/P&L → risk → walk-forward OOS
C++:    parent order → TWAP/VWAP/POV → matching engine → trades → execution analytics
Math:   Black-Scholes → Greeks → implied volatility
Proof:  unit/integration tests → sanitizers → deterministic Release benchmarks
```

## Where to look first

1. `python/quant_system/backtest/engine.py` shows orchestration without strategy or P&L logic.
2. `python/quant_system/portfolio/position.py` contains long/short partial-close and reversal math.
3. `python/quant_system/research/walk_forward.py` demonstrates training-only selection and frozen
   out-of-sample parameters.
4. `cpp/src/order_book.cpp` implements best-price then FIFO matching.
5. `cpp/src/matching_engine.cpp` owns validation, lifecycle, cancel/replace, and reports.
6. `cpp/src/execution/` converts parent intent into deterministic child orders.
7. `cpp/benchmarks/` separates workload generation, throughput timing, latency recording, and
   reporting.

## Correctness properties

- Corrupt OHLCV values, duplicates, NaN, infinity, and invalid relationships fail at ingestion.
- A signal generated from bar `t` cannot fill before bar `t+1` for the same symbol.
- Portfolio state changes only after a fill; strategy and execution do not mutate accounting.
- Average-price accounting handles increases, partial closes, full closes, and both reversal
  directions.
- Risk limits evaluate the hypothetical post-trade portfolio and allow genuinely risk-reducing
  orders.
- Walk-forward parameter selection sees training data only; test data is evaluation-only.
- Native matching uses integer ticks, ordered price levels, FIFO queues, stable sequence numbers,
  and execution at the resting price.
- Parent executed quantity equals actual child fills, never submitted quantity.
- Positive execution slippage always means unfavorable for BUY and SELL.
- Performance claims are backed by Release measurements, reproducible seeds, checksums, and
  invariant validation.

## Measured evidence

On the documented Apple M4/macOS Release environment, the required five-run mixed 1M workload
measured 954,362 operations/s before capacity hints and 1,093,437 operations/s after the measured
reservation experiment. The same workload produced the same 150,170 trades, 299,208 active
orders, and checksum `10997445773294864847` in both cases.

| Metric | Baseline | Capacity reserved |
| --- | ---: | ---: |
| Throughput | 954,362 ops/s | 1,093,437 ops/s |
| p50 | 916 ns | 833 ns |
| p95 | 1,833 ns | 1,625 ns |
| p99 | 2,125 ns | 1,709 ns |

These numbers describe this machine and workload; they are not exchange-grade or HFT claims.
Full methodology and raw-result paths are documented in `docs/performance.md`.

## Build and verify

```bash
python3.12 -m venv .venv
source .venv/bin/activate
python -m pip install -e '.[dev]'

pytest
ruff check .
mypy python/quant_system

cmake -S cpp -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Run examples:

```bash
python scripts/run_strategy_backtest.py --strategy all --symbol AAPL
python scripts/run_portfolio_example.py
python scripts/run_walk_forward.py
./build/execution_example
./build/options_example
```

Run the primary benchmark:

```bash
cmake -S cpp -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
./build-release/benchmarks/quant_engine_benchmarks \
  --benchmark mixed --operations 1000000 --seed 42 --runs 5
```

## Engineering lessons

- Boundaries matter more than inheritance: strategy, risk, execution, and accounting have distinct
  authority.
- Market chronology is an invariant, not a convention left to individual strategies.
- Price priority and time priority need different data structures.
- Submitted quantity is intent; fills are truth.
- Statistical risk and pre-trade risk answer different questions.
- Performance is empirical: baseline, profile, change, re-measure, and revalidate correctness.

## Deliberate limitations

The system omits live connectivity, brokerage reconciliation, full margin/borrow accounting,
self-trade prevention, advanced time-in-force, American/exotic derivatives, volatility surfaces,
and Python/C++ bindings. Those are explicit extension points, not hidden incomplete features.
