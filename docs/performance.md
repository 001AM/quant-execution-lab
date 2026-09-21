# Performance Engineering

## Methodology

Performance is treated as a measured property of a specific build and workload. The workflow used
for Set 11 was:

```text
correctness → unreserved baseline → sample profile → one capacity-reserve experiment
            → optimized measurement → correctness and checksum verification
```

All published measurements use `std::chrono::steady_clock` in a CMake `Release` build. Workload
generation, engine setup, invariant scans, checksum calculation, report formatting, and CSV I/O
are outside timed regions. Throughput mode places one timer around the entire command stream.
Latency mode is a separate run with one timer around each command; its clock calls therefore add
measurement overhead and its throughput is not substituted for bulk throughput.

Each scenario runs 10,000 untimed operations before measurement. The recorded matrix uses three
measured throughput runs and reports the median. The CLI defaults to five runs. Latencies are
stored as nanosecond durations, but that representation does not imply one-nanosecond timer
accuracy. Percentiles use nearest rank: for sorted `N` samples, percentile `p` selects
`ceil(p × N) - 1` using a zero-based index.

## Environment

Measurements below were captured on 2026-09-21 with:

| Field | Value |
|---|---|
| Machine | MacBook Air, Apple M4, 10 cores, 16 GB memory |
| OS | macOS 26.6.2, arm64 |
| Compiler | Apple Clang 21.0.0 (`clang-2100.3.34.2`) |
| Language | C++20 |
| Build | CMake Release/toolchain optimization defaults |
| Seed | 42 |
| Timed runs | 3, median throughput |

CPU frequency scaling, thermal state, scheduler activity, filesystem placement, and other machine
load were not controlled. Results are a reproducible local baseline, not universal latency claims.

## Workloads

- **Submit:** non-crossing LIMIT orders, 100 total bid/ask levels.
- **Cancel:** valid active IDs at a shared level; a separate scenario removes the final order at
  each level.
- **Modify:** same-price decrease, same-price increase, and price change are measured separately.
- **Matching:** aggressive LIMIT or MARKET orders against pre-populated liquidity. Incoming-order
  throughput and generated fills are both recorded.
- **Level walk:** each incoming order consumes four orders across four distinct price levels.
- **Mixed:** deterministic target mix of 60% resting LIMIT submits, 15% cancellations, 10%
  modifications, 10% crossing LIMIT orders, and 5% MARKET orders. Exact generated counts are
  included in every report.
- **Depth:** submission and best-price access are measured at 10, 100, 1,000, and 10,000 levels.

Workload setup is never part of the timed interval. Generated commands are validated during the
run: a supposedly valid submit, cancel, or modify that is rejected aborts the benchmark.

## Optimized 1M results

These are actual `--reserve` results from `benchmark_results/optimized.csv`:

| Workload | Throughput ops/s | p50 ns | p95 ns | p99 ns | Fills |
|---|---:|---:|---:|---:|---:|
| Submit | 1,029,243 | 958 | 1,083 | 1,167 | 0 |
| Cancel, shared level | 765,262 | 1,250 | 1,459 | 1,625 | 0 |
| Modify, decrease | 3,168,649 | 333 | 417 | 458 | 0 |
| Modify, increase | 1,565,496 | 625 | 750 | 833 | 0 |
| Modify, price | 970,219 | 1,042 | 1,208 | 1,292 | 0 |
| Match, one fill/order | 646,323 | 1,541 | 1,625 | 1,916 | 1,000,000 |
| Mixed | 1,074,053 | 833 | 1,666 | 1,750 | 150,170 |

The 10,000-order level-walk scenario produced 40,000 fills: 243,799 incoming orders/s with p50
4,167 ns, p95 4,334 ns, and p99 4,458 ns. A single MARKET order exhausting 100,000 resting orders
took 61.8 ms and produced 100,000 fills. These figures demonstrate why matching reports both
incoming commands and fill work.

## Scaling

Optimized deterministic mixed workload:

| Operations | Elapsed s | Throughput ops/s | p50 ns | p95 ns | p99 ns |
|---:|---:|---:|---:|---:|---:|
| 10,000 | 0.0093 | 1,078,676 | 833 | 1,625 | 1,709 |
| 100,000 | 0.0930 | 1,074,935 | 833 | 1,625 | 1,709 |
| 1,000,000 | 0.9311 | 1,074,053 | 833 | 1,666 | 1,750 |

Resting submission with 100,000 orders showed the expected ordered-level cost trend:

| Total price levels | Throughput ops/s |
|---:|---:|
| 10 | 1,185,147 |
| 100 | 1,115,808 |
| 1,000 | 1,039,646 |
| 10,000 | 988,538 |

Best bid/ask access remained approximately 18.6–18.8 million paired reads/s from 10 through 10,000
levels because the first ordered-map node supplies the best price. Recorded p50 was 42–83 ns;
some samples rounded to zero at the clock's effective resolution, reinforcing that duration units
are not timer-accuracy claims.

## Profiling findings

macOS `sample` profiled an unreserved 1M mixed workload. Of 2,557 main-thread samples, 1,789 were
inside benchmark command processing, 1,184 in `MatchingEngine::submit`, 548 in
`OrderBook::add_limit_order`, 345 in `OrderBook::rest`, and 189 in `PriceLevel::add_order`.
The expanded call tree showed per-level `unordered_map` insertion, node allocation, and rehashing.
This evidence supported a capacity-reserve experiment; it did not justify replacing `std::map`,
`std::list`, or the ownership model.

For deeper profiling:

```bash
# macOS: Instruments GUI, or a lightweight command-line sample
./build-release/benchmarks/quant_engine_benchmarks \
  --benchmark mixed --operations 1000000 --runs 10 --throughput-only &
sample $! 5 -file mixed.sample.txt

# Linux (optional; not a build dependency)
perf record -g ./build-release/benchmarks/quant_engine_benchmarks \
  --benchmark mixed --operations 1000000 --runs 10 --throughput-only
perf report
```

Inspect time in ordered maps, active-ID hashes, per-level indexes, allocation/deallocation, trade
vector growth, and order history before proposing another change.

## Optimization experiment

The retained optimization adds an opt-in capacity hint that reserves active/completed order hash
tables, trade history, and per-level ID indexes. Normal callers retain default behavior. Benchmark
mode enables it with `--reserve`, where replay size and expected depth are known.

Dedicated adjacent five-run mixed 1M comparison:

| Metric | Baseline | Reserved | Improvement |
|---|---:|---:|---:|
| Throughput ops/s | 954,362 | 1,093,437 | +14.57% |
| p50 ns | 916 | 833 | +9.06% |
| p95 ns | 1,833 | 1,625 | +11.35% |
| p99 ns | 2,125 | 1,709 | +19.58% |

Both modes produced checksum `10997445773294864847`, 150,170 trades, 299,208 active orders, one
bid level, and a passing invariant scan. The tradeoff is additional up-front memory when callers
overestimate capacity; this is why reserve remains explicit instead of automatic.

## Correctness and regression checks

The 1M mixed stress workload was run twice independently. Both produced checksum
`10997445773294864847`, 150,170 trades, 299,208 active orders, one price level, and passing book
invariants. Unit tests cover deterministic workloads, ID validity, empty/single latency recorders,
nearest-rank p50/p95/p99, throughput/latency change formulas, CSV serialization, checksum
determinism, and a 10K executable mixed workload.

CI does not fail on absolute nanosecond thresholds. Optional regression checking compares measured
throughput with a supplied baseline and uses a configurable percentage threshold:

```bash
./build-release/benchmarks/quant_engine_benchmarks \
  --benchmark mixed --operations 100000 --seed 42 --runs 5 \
  --baseline-throughput 900000 --max-regression-percent 25
```

## Reproduce

```bash
cmake -S cpp -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release

./build-release/benchmarks/quant_engine_benchmarks \
  --benchmark mixed --operations 1000000 --seed 42 --runs 5

./build-release/benchmarks/quant_engine_benchmarks \
  --benchmark all --matrix --seed 42 --runs 3 \
  --output benchmark_results/baseline.csv

./build-release/benchmarks/quant_engine_benchmarks \
  --benchmark all --matrix --seed 42 --runs 3 --reserve \
  --output benchmark_results/optimized.csv
```

`--throughput-only` skips per-command timing; `--price-levels` changes depth; `--benchmark best`,
`matching-walk`, and `market-exhaust` expose specialized scenarios. Sanitizers are deliberately run
in a separate Debug build and are never used for published performance numbers.

## Limitations

This is a single-process simulator benchmark, not an exchange or HFT latency claim. It does not
model networking, serialization, persistence, concurrency, NUMA, CPU affinity, production market
data, or participant-level risk. No lock-free structures, custom allocators, SIMD, kernel bypass,
or thread pinning were introduced. Future optimization should repeat the same baseline/profile/
change/re-measure/correctness sequence.
