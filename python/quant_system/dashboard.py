"""Local web dashboard for the research and execution workflows."""

from __future__ import annotations

import argparse
import io
import math
import subprocess
import threading
import time
from collections.abc import Sequence
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import asdict
from pathlib import Path
from typing import Any, Literal, cast

import pandas as pd
import uvicorn
import yfinance as yf  # type: ignore[import-untyped]
from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, ConfigDict, Field

from quant_system.backtest import BacktestEngine, FillEvent, OrderSide
from quant_system.data import MarketDataLoader
from quant_system.execution import PercentageCommission, PercentageSlippage
from quant_system.portfolio import Portfolio
from quant_system.research import walk_forward_evaluate
from quant_system.strategy import (
    MeanReversionStrategy,
    MomentumStrategy,
    MovingAverageCrossoverStrategy,
    Strategy,
)

PROJECT_ROOT = Path(__file__).resolve().parents[2]
WEB_ROOT = Path(__file__).with_name("web")
MAX_REQUEST_BYTES = 10 * 1024 * 1024
YAHOO_CACHE_TTL_SECONDS = 15 * 60
_YAHOO_CACHE: dict[tuple[str, str], tuple[float, pd.DataFrame]] = {}
_YAHOO_CACHE_LOCK = threading.Lock()


def _number(payload: dict[str, Any], name: str, default: float, *, minimum: float = 0) -> float:
    value = payload.get(name, default)
    if isinstance(value, bool) or not isinstance(value, int | float):
        raise ValueError(f"{name} must be a number")
    parsed = float(value)
    if not math.isfinite(parsed) or parsed < minimum:
        raise ValueError(f"{name} must be at least {minimum}")
    return parsed


def _integer(payload: dict[str, Any], name: str, default: int, *, minimum: int = 1) -> int:
    value = payload.get(name, default)
    if isinstance(value, bool) or not isinstance(value, int | float) or int(value) != value:
        raise ValueError(f"{name} must be a whole number")
    parsed = int(value)
    if parsed < minimum:
        raise ValueError(f"{name} must be at least {minimum}")
    return parsed


def _finite(value: Any) -> float | int | None:
    if value is None:
        return None
    parsed = float(value)
    return parsed if math.isfinite(parsed) else None


YAHOO_PERIODS = {"1mo", "3mo", "6mo", "1y", "2y", "5y"}
INDIAN_INDEX_ALIASES = {
    "NIFTY": "^NSEI",
    "NIFTY50": "^NSEI",
    "BANKNIFTY": "^NSEBANK",
    "NIFTYBANK": "^NSEBANK",
    "SENSEX": "^BSESN",
}

INDIAN_MARKET_INDICES = {
    "Nifty 50": "^NSEI",
    "Sensex": "^BSESN",
    "Bank Nifty": "^NSEBANK",
}

# Equal-weight baskets make sector comparison reliable when the free provider does
# not expose enough history for an official sector index. Constituents are shown
# in the UI so the proxy is explicit rather than presented as an exchange index.
INDIAN_SECTOR_BASKETS = {
    "Banking & Finance": ("HDFCBANK.NS", "ICICIBANK.NS", "SBIN.NS"),
    "Information Technology": ("TCS.NS", "INFY.NS", "HCLTECH.NS"),
    "Energy": ("RELIANCE.NS", "ONGC.NS", "NTPC.NS"),
    "Automobiles": ("MARUTI.NS", "M&M.NS", "EICHERMOT.NS"),
    "Pharmaceuticals": ("SUNPHARMA.NS", "DRREDDY.NS", "CIPLA.NS"),
    "Consumer Staples": ("HINDUNILVR.NS", "ITC.NS", "NESTLEIND.NS"),
    "Metals": ("TATASTEEL.NS", "HINDALCO.NS", "JSWSTEEL.NS"),
    "Real Estate": ("DLF.NS", "GODREJPROP.NS", "OBEROIRLTY.NS"),
    "Industrials": ("LT.NS", "SIEMENS.NS", "ABB.NS"),
    "Telecom": ("BHARTIARTL.NS", "INDUSTOWER.NS", "TATACOMM.NS"),
}


def _normalize_yahoo_symbol(symbol: str, exchange: str) -> str:
    compact_symbol = symbol.replace(" ", "").upper()
    aliased = INDIAN_INDEX_ALIASES.get(compact_symbol, compact_symbol)
    if aliased.startswith("^") or "." in aliased:
        return aliased
    if exchange == "nse":
        return f"{aliased}.NS"
    if exchange == "bse":
        return f"{aliased}.BO"
    if exchange not in {"auto", "us"}:
        raise ValueError("exchange must be auto, us, nse, or bse")
    return aliased


def _currency_for_symbol(symbol: str) -> str:
    is_indian = symbol.endswith((".NS", ".BO")) or symbol.startswith(("^NSE", "^BSE"))
    return "INR" if is_indian else "USD"


def _exchange_for_symbol(symbol: str) -> str:
    if symbol.endswith(".NS") or symbol.startswith("^NSE"):
        return "NSE"
    if symbol.endswith(".BO") or symbol.startswith("^BSE"):
        return "BSE"
    return "Global"


def _download_yahoo(symbol: str, period: str) -> pd.DataFrame:
    """Download daily bars with a bounded, thread-safe time-based cache."""

    key = (symbol, period)
    now = time.monotonic()
    with _YAHOO_CACHE_LOCK:
        cached = _YAHOO_CACHE.get(key)
        if cached is not None and cached[0] > now:
            return cached[1].copy(deep=True)

    try:
        frame = yf.Ticker(symbol).history(
            period=period,
            interval="1d",
            actions=False,
            auto_adjust=False,
            repair=False,
            timeout=12,
        )
    except Exception as exc:
        raise ValueError(f"Yahoo Finance could not load {symbol}: {exc}") from exc
    if frame.empty:
        raise ValueError(f"Yahoo Finance returned no daily price history for {symbol}")
    frame = frame.reset_index()
    timestamp_column = "Date" if "Date" in frame else "Datetime"
    if timestamp_column not in frame:
        raise ValueError("Yahoo Finance response did not include timestamps")
    canonical = frame.rename(
        columns={
            timestamp_column: "timestamp",
            "Open": "open",
            "High": "high",
            "Low": "low",
            "Close": "close",
            "Volume": "volume",
        }
    )[["timestamp", "open", "high", "low", "close", "volume"]]
    canonical.insert(1, "symbol", symbol)
    result = cast(pd.DataFrame, canonical.dropna().copy(deep=True))
    with _YAHOO_CACHE_LOCK:
        if len(_YAHOO_CACHE) >= 96:
            oldest_key = min(_YAHOO_CACHE, key=lambda item: _YAHOO_CACHE[item][0])
            del _YAHOO_CACHE[oldest_key]
        _YAHOO_CACHE[key] = (now + YAHOO_CACHE_TTL_SECONDS, result.copy(deep=True))
    return result


def _market_instrument(symbol: str, period: str) -> dict[str, Any]:
    data = _download_yahoo(symbol, period)
    first_close = float(data["close"].iloc[0])
    last_close = float(data["close"].iloc[-1])
    return {
        "symbol": symbol,
        "last_close": last_close,
        "change": last_close / first_close - 1.0,
        "bars": len(data),
        "as_of": pd.Timestamp(data["timestamp"].iloc[-1]).isoformat(),
    }


def indian_market_overview(period: str = "1mo") -> dict[str, Any]:
    """Return broad-index and transparent NSE sector-basket performance."""

    if period not in {"1mo", "3mo", "6mo", "1y"}:
        raise ValueError("sector period must be 1mo, 3mo, 6mo, or 1y")

    requested_symbols = set(INDIAN_MARKET_INDICES.values())
    for constituents in INDIAN_SECTOR_BASKETS.values():
        requested_symbols.update(constituents)

    instruments: dict[str, dict[str, Any]] = {}
    failures: dict[str, str] = {}
    with ThreadPoolExecutor(max_workers=8, thread_name_prefix="market-overview") as pool:
        futures = {
            pool.submit(_market_instrument, symbol, period): symbol
            for symbol in requested_symbols
        }
        for future in as_completed(futures):
            symbol = futures[future]
            try:
                instruments[symbol] = future.result()
            except (ValueError, TypeError, OSError) as exc:
                failures[symbol] = str(exc)

    indices = [
        {"name": name, **instruments[symbol]}
        for name, symbol in INDIAN_MARKET_INDICES.items()
        if symbol in instruments
    ]
    sectors: list[dict[str, Any]] = []
    for name, symbols in INDIAN_SECTOR_BASKETS.items():
        members = [
            {"symbol": symbol, **instruments[symbol]}
            for symbol in symbols
            if symbol in instruments
        ]
        if not members:
            continue
        members.sort(key=lambda member: member["change"], reverse=True)
        sectors.append(
            {
                "name": name,
                "change": sum(member["change"] for member in members) / len(members),
                "breadth": sum(member["change"] >= 0 for member in members) / len(members),
                "available": len(members),
                "total": len(symbols),
                "constituents": members,
            }
        )
    sectors.sort(key=lambda sector: sector["change"], reverse=True)
    available_members = sum(sector["available"] for sector in sectors)
    advancing_members = sum(
        member["change"] >= 0
        for sector in sectors
        for member in sector["constituents"]
    )
    timestamps = [item["as_of"] for item in instruments.values()]
    return {
        "provider": "Yahoo Finance",
        "period": period,
        "currency": "INR",
        "methodology": "Equal-weight return of three large NSE companies per sector",
        "as_of": max(timestamps) if timestamps else None,
        "indices": indices,
        "sectors": sectors,
        "market_breadth": advancing_members / available_members if available_members else None,
        "available_instruments": len(instruments),
        "requested_instruments": len(requested_symbols),
        "failures": failures,
    }


def _load_data(payload: dict[str, Any]) -> tuple[pd.DataFrame, str, str]:
    symbol = str(payload.get("symbol", "AAPL")).strip().upper()
    if not symbol:
        raise ValueError("symbol is required")

    source_mode = str(payload.get("source", "yahoo")).strip().lower()
    if source_mode == "yahoo":
        exchange = str(payload.get("exchange", "auto")).strip().lower()
        symbol = _normalize_yahoo_symbol(symbol, exchange)
        period = str(payload.get("period", "6mo")).strip().lower()
        if period not in YAHOO_PERIODS:
            raise ValueError("period must be 1mo, 3mo, 6mo, 1y, 2y, or 5y")
        data = MarketDataLoader().load_frame(_download_yahoo(symbol, period), symbol=symbol)
        return data, f"Yahoo Finance · {symbol} · {period}", "yahoo"
    if source_mode != "csv":
        raise ValueError("source must be yahoo or csv")

    csv_text = payload.get("csv_text")
    loader = MarketDataLoader()
    if csv_text:
        if not isinstance(csv_text, str):
            raise ValueError("uploaded CSV must be text")
        try:
            frame = pd.read_csv(io.StringIO(csv_text), dtype=str)
        except (pd.errors.ParserError, pd.errors.EmptyDataError) as exc:
            raise ValueError(f"unable to read uploaded CSV: {exc}") from exc
        return loader.load_frame(frame, symbol=symbol), "Uploaded CSV", "csv"

    requested = str(payload.get("data_path", "data/sample/AAPL.csv")).strip()
    candidate = (PROJECT_ROOT / requested).resolve()
    try:
        candidate.relative_to(PROJECT_ROOT)
    except ValueError as exc:
        raise ValueError("data path must stay inside the project") from exc
    if candidate.suffix.lower() != ".csv":
        raise ValueError("data path must point to a CSV file")
    return (
        loader.load_csv(candidate, symbol=symbol),
        str(candidate.relative_to(PROJECT_ROOT)),
        "csv",
    )


def _strategies(selection: str, *, allow_short: bool) -> dict[str, Strategy]:
    available: dict[str, Strategy] = {
        "momentum": MomentumStrategy(lookback=5, threshold=0.01),
        "mean_reversion": MeanReversionStrategy(window=5, entry_z=1.5, exit_z=0.5),
        "ma_crossover": MovingAverageCrossoverStrategy(
            fast_window=3,
            slow_window=7,
            allow_short=allow_short,
        ),
    }
    if selection == "all":
        return available
    key = selection.replace("-", "_")
    if key not in available:
        raise ValueError("strategy must be all, momentum, mean-reversion, or ma-crossover")
    return {key: available[key]}


def dataset_summary(payload: dict[str, Any]) -> dict[str, Any]:
    data, source, provider = _load_data(payload)
    first_close = float(data["close"].iloc[0])
    last_close = float(data["close"].iloc[-1])
    return {
        "source": source,
        "provider": provider,
        "symbol": str(data["symbol"].iloc[0]),
        "exchange": _exchange_for_symbol(str(data["symbol"].iloc[0])),
        "currency": _currency_for_symbol(str(data["symbol"].iloc[0])),
        "bars": len(data),
        "start": pd.Timestamp(data["timestamp"].iloc[0]).isoformat(),
        "end": pd.Timestamp(data["timestamp"].iloc[-1]).isoformat(),
        "last_close": last_close,
        "price_change": last_close / first_close - 1.0,
        "period_high": float(data["high"].max()),
        "period_low": float(data["low"].min()),
        "average_volume": float(data["volume"].mean()),
        "prices": [
            {
                "timestamp": pd.Timestamp(str(row.timestamp)).isoformat(),
                "open": float(cast(float, row.open)),
                "high": float(cast(float, row.high)),
                "low": float(cast(float, row.low)),
                "close": float(cast(float, row.close)),
                "volume": float(cast(float, row.volume)),
            }
            for row in data.itertuples(index=False)
        ],
    }


def run_backtest(payload: dict[str, Any]) -> dict[str, Any]:
    data, source, provider = _load_data(payload)
    initial_capital = _number(payload, "initial_capital", 100_000, minimum=1)
    quantity = _integer(payload, "quantity", 100)
    commission_rate = _number(payload, "commission", 0.001)
    slippage_rate = _number(payload, "slippage", 0.0005)
    allow_short = payload.get("allow_short", True)
    if not isinstance(allow_short, bool):
        raise ValueError("allow_short must be true or false")
    selection = str(payload.get("strategy", "all"))
    benchmark = float(data["close"].iloc[-1] / data["close"].iloc[0] - 1.0)
    runs: list[dict[str, Any]] = []

    for name, strategy in _strategies(selection, allow_short=allow_short).items():
        result = BacktestEngine(
            data=data,
            strategy=strategy,
            initial_capital=initial_capital,
            quantity=quantity,
            commission=PercentageCommission(commission_rate),
            slippage=PercentageSlippage(slippage_rate),
            allow_short_selling=allow_short,
        ).run()
        runs.append(
            {
                "strategy": name,
                "ending_equity": result.final_equity,
                "total_return": result.total_return,
                "benchmark_return": benchmark,
                "sharpe_ratio": _finite(result.performance["sharpe_ratio"]),
                "max_drawdown": _finite(result.performance["max_drawdown"]),
                "volatility": _finite(result.performance["annualized_volatility"]),
                "fills": len(result.fills),
                "rejected_orders": len(result.rejected_orders),
                "equity": [
                    {
                        "timestamp": pd.Timestamp(str(timestamp)).isoformat(),
                        "value": float(value),
                    }
                    for timestamp, value in result.equity_curve.items()
                ],
                "trades": [
                    {
                        "timestamp": fill.timestamp.isoformat(),
                        "side": fill.side.value,
                        "quantity": fill.quantity,
                        "price": fill.fill_price,
                        "commission": fill.commission,
                    }
                    for fill in result.fills
                ],
            }
        )

    return {
        "source": source,
        "provider": provider,
        "symbol": str(data["symbol"].iloc[0]),
        "currency": _currency_for_symbol(str(data["symbol"].iloc[0])),
        "bars": len(data),
        "runs": runs,
    }


def run_walk_forward(payload: dict[str, Any]) -> dict[str, Any]:
    data, source, provider = _load_data(payload)
    initial_capital = _number(payload, "initial_capital", 100_000, minimum=1)
    quantity = _integer(payload, "quantity", 100)
    train_size = _integer(payload, "train_size", 15, minimum=2)
    test_size = _integer(payload, "test_size", 5, minimum=2)

    def momentum_factory(lookback: int, threshold: float) -> Strategy:
        return MomentumStrategy(lookback=lookback, threshold=threshold)

    summary = walk_forward_evaluate(
        data,
        momentum_factory,
        {"lookback": [3, 5, 8], "threshold": [0.005, 0.01]},
        train_size=train_size,
        test_size=test_size,
        step_size=test_size,
        objective="total_return",
        initial_capital=initial_capital,
        quantity=quantity,
        commission=PercentageCommission(0.001),
        slippage=PercentageSlippage(0.0005),
        allow_short_selling=True,
    )
    return {
        "source": source,
        "provider": provider,
        "currency": _currency_for_symbol(str(data["symbol"].iloc[0])),
        "performance": {key: _finite(value) for key, value in summary.performance.items()},
        "risk": {key: _finite(value) for key, value in asdict(summary.risk_report).items()},
        "fills": summary.total_oos_fills,
        "folds": [
            {
                "fold": fold.fold,
                "train": f"{fold.train_start} → {fold.train_end}",
                "test": f"{fold.test_start} → {fold.test_end}",
                "parameters": fold.selected_parameters,
                "train_score": _finite(fold.train_score),
                "test_return": _finite(fold.test_performance["total_return"]),
                "fills": fold.number_of_fills,
            }
            for fold in summary.folds
        ],
        "equity": [
            {"timestamp": pd.Timestamp(str(timestamp)).isoformat(), "value": float(value)}
            for timestamp, value in summary.combined_oos_equity.items()
        ],
    }


def run_portfolio_snapshot(payload: dict[str, Any]) -> dict[str, Any]:
    """Mark a selected-market position from its first open to latest close."""

    data, source, provider = _load_data(payload)
    initial_capital = _number(payload, "portfolio_capital", 100_000, minimum=1)
    quantity = _integer(payload, "portfolio_quantity", 10)
    commission_rate = _number(payload, "commission", 0.001)
    symbol = str(data["symbol"].iloc[0])
    entry_price = float(data["open"].iloc[0])
    market_price = float(data["close"].iloc[-1])
    commission = entry_price * quantity * commission_rate
    required_cash = entry_price * quantity + commission
    if required_cash > initial_capital:
        raise ValueError(
            f"position requires {required_cash:,.2f}, above available capital "
            f"of {initial_capital:,.2f}; reduce quantity or increase capital"
        )

    entry_time = pd.Timestamp(data["timestamp"].iloc[0]).to_pydatetime()
    mark_time = pd.Timestamp(data["timestamp"].iloc[-1]).to_pydatetime()
    portfolio = Portfolio(initial_capital)
    portfolio.process_fill(
        FillEvent(
            "dashboard-position",
            entry_time,
            symbol,
            OrderSide.BUY,
            quantity,
            entry_price,
            commission,
            0.0,
        )
    )
    portfolio.update_market_price(symbol, market_price)
    portfolio.record_snapshot(mark_time)
    return {
        "source": source,
        "provider": provider,
        "currency": _currency_for_symbol(symbol),
        "symbol": symbol,
        "entry_timestamp": entry_time.isoformat(),
        "mark_timestamp": mark_time.isoformat(),
        "entry_price": entry_price,
        "market_price": market_price,
        "position_return": market_price / entry_price - 1.0,
        "initial_capital": initial_capital,
        "cash": portfolio.cash,
        "equity": portfolio.equity,
        "realized_pnl": portfolio.gross_realized_pnl,
        "unrealized_pnl": portfolio.unrealized_pnl,
        "commission": portfolio.total_commission,
        "gross_exposure": portfolio.gross_exposure,
        "net_exposure": portfolio.net_exposure,
        "positions": [
            {
                "symbol": position.symbol,
                "quantity": position.quantity,
                "average_price": position.average_price,
            }
            for position in portfolio.positions.values()
        ],
    }


def run_risk_snapshot(payload: dict[str, Any]) -> dict[str, Any]:
    """Calculate a real-data RMS snapshot for one selected-market position."""

    data, source, provider = _load_data(payload)
    if len(data) < 2:
        raise ValueError("risk monitoring requires at least two validated market bars")

    capital = _number(payload, "risk_capital", 1_000_000, minimum=1)
    quantity = _integer(payload, "risk_quantity", 10)
    commission_rate = _number(payload, "commission", 0.001)
    max_position_pct = _number(payload, "max_position_pct", 0.35, minimum=0.01)
    max_leverage = _number(payload, "max_leverage", 1.0, minimum=0.01)
    max_drawdown_limit = _number(payload, "max_drawdown_pct", 0.20, minimum=0.01)
    max_var_pct = _number(payload, "max_var_pct", 0.03, minimum=0.001)

    symbol = str(data["symbol"].iloc[0])
    entry_price = float(data["open"].iloc[0])
    previous_close = float(data["close"].iloc[-2])
    market_price = float(data["close"].iloc[-1])
    commission = entry_price * quantity * commission_rate
    market_value = market_price * quantity
    cash = capital - entry_price * quantity - commission
    equity = cash + market_value
    equity_denominator = max(abs(equity), 1e-9)
    unrealized_pnl = (market_price - entry_price) * quantity - commission
    daily_pnl = (market_price - previous_close) * quantity
    leverage = market_value / equity_denominator
    position_pct = market_value / equity_denominator

    returns = data["close"].astype(float).pct_change().dropna()
    annualized_volatility = float(returns.std() * math.sqrt(252)) if len(returns) > 1 else 0.0
    var_rate = max(0.0, -float(returns.quantile(0.05))) if not returns.empty else 0.0
    var_95 = var_rate * market_value
    var_95_pct = var_95 / equity_denominator
    drawdown_series = data["close"].astype(float) / data["close"].astype(float).cummax() - 1.0
    max_drawdown = abs(float(drawdown_series.min()))

    limit_inputs = (
        ("Position concentration", position_pct, max_position_pct, "percent"),
        ("Portfolio leverage", leverage, max_leverage, "multiple"),
        ("Historical drawdown", max_drawdown, max_drawdown_limit, "percent"),
        ("One-day historical VaR", var_95_pct, max_var_pct, "percent"),
    )
    limits: list[dict[str, Any]] = []
    for name, current, limit, unit in limit_inputs:
        utilization = current / limit
        status = "breach" if utilization >= 1 else "warning" if utilization >= 0.8 else "healthy"
        limits.append(
            {
                "name": name,
                "current": current,
                "limit": limit,
                "unit": unit,
                "utilization": utilization,
                "status": status,
            }
        )

    breaches = [limit for limit in limits if limit["status"] == "breach"]
    warnings = [limit for limit in limits if limit["status"] == "warning"]
    overall_status = "breach" if breaches else "warning" if warnings else "healthy"
    max_utilization = max(limit["utilization"] for limit in limits)
    alerts = [
        {
            "severity": limit["status"],
            "title": (
                f"{limit['name']} "
                f"{'breached' if limit['status'] == 'breach' else 'near limit'}"
            ),
            "detail": f"{limit['utilization'] * 100:.0f}% of configured limit is in use.",
        }
        for limit in limits
        if limit["status"] != "healthy"
    ]
    if not alerts:
        alerts.append(
            {
                "severity": "healthy",
                "title": "All monitored limits are within range",
                "detail": "No concentration, leverage, drawdown, or VaR threshold is near breach.",
            }
        )

    return {
        "source": source,
        "provider": provider,
        "symbol": symbol,
        "exchange": _exchange_for_symbol(symbol),
        "currency": _currency_for_symbol(symbol),
        "as_of": pd.Timestamp(data["timestamp"].iloc[-1]).isoformat(),
        "status": overall_status,
        "risk_score": min(max_utilization * 100, 999.0),
        "capital": capital,
        "cash": cash,
        "equity": equity,
        "daily_pnl": daily_pnl,
        "unrealized_pnl": unrealized_pnl,
        "gross_exposure": market_value,
        "net_exposure": market_value,
        "available_capital": cash,
        "leverage": leverage,
        "annualized_volatility": annualized_volatility,
        "max_drawdown": max_drawdown,
        "var_95": var_95,
        "var_95_pct": var_95_pct,
        "limits": limits,
        "alerts": alerts,
        "position": {
            "symbol": symbol,
            "quantity": quantity,
            "side": "LONG",
            "entry_price": entry_price,
            "market_price": market_price,
            "market_value": market_value,
            "unrealized_pnl": unrealized_pnl,
            "position_pct": position_pct,
        },
    }


def run_execution_simulation(payload: dict[str, Any]) -> dict[str, Any]:
    """Run native execution algorithms using the selected market's latest close."""

    data, source, provider = _load_data(payload)
    symbol = str(data["symbol"].iloc[0])
    quantity = _integer(payload, "execution_quantity", 1_000, minimum=4)
    market_price = float(data["close"].iloc[-1])
    price_ticks = round(market_price * 100)
    candidates = (
        PROJECT_ROOT / "build-release" / "execution_example",
        PROJECT_ROOT / "build" / "execution_example",
    )
    executable = next((path for path in candidates if path.is_file()), None)
    if executable is None:
        raise ValueError(
            "C++ example is not built. Run: cmake -S cpp -B build-release "
            "-DCMAKE_BUILD_TYPE=Release && cmake --build build-release"
        )
    source_path = PROJECT_ROOT / "cpp" / "examples" / "execution_example.cpp"
    if executable.stat().st_mtime < source_path.stat().st_mtime:
        raise ValueError("C++ execution example is out of date; rebuild build-release")
    completed = subprocess.run(
        [
            str(executable),
            "--symbol",
            symbol,
            "--quantity",
            str(quantity),
            "--price-ticks",
            str(price_ticks),
        ],
        cwd=PROJECT_ROOT,
        capture_output=True,
        check=False,
        text=True,
        timeout=20,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or "C++ execution example failed")
    return {
        "output": completed.stdout.strip(),
        "binary": str(executable.relative_to(PROJECT_ROOT)),
        "source": source,
        "provider": provider,
        "symbol": symbol,
        "currency": _currency_for_symbol(symbol),
        "quantity": quantity,
        "market_price": market_price,
        "price_ticks": price_ticks,
    }


class DataRequest(BaseModel):
    model_config = ConfigDict(extra="forbid", str_strip_whitespace=True)

    source: Literal["yahoo", "csv"] = "yahoo"
    symbol: str = Field(default="AAPL", min_length=1, max_length=30)
    exchange: Literal["auto", "us", "nse", "bse"] = "auto"
    period: Literal["1mo", "3mo", "6mo", "1y", "2y", "5y"] = "6mo"
    data_path: str = Field(default="data/sample/AAPL.csv", max_length=512)
    csv_text: str | None = Field(default=None, max_length=MAX_REQUEST_BYTES)


class BacktestRequest(DataRequest):
    strategy: Literal["all", "momentum", "mean-reversion", "ma-crossover"] = "all"
    initial_capital: float = Field(default=100_000, gt=0)
    quantity: int = Field(default=100, ge=1)
    commission: float = Field(default=0.001, ge=0)
    slippage: float = Field(default=0.0005, ge=0)
    allow_short: bool = True


class ResearchRequest(BacktestRequest):
    train_size: int = Field(default=60, ge=2)
    test_size: int = Field(default=20, ge=2)


class PortfolioRequest(DataRequest):
    portfolio_capital: float = Field(default=100_000, gt=0)
    portfolio_quantity: int = Field(default=10, ge=1)
    commission: float = Field(default=0.001, ge=0)


class ExecutionRequest(DataRequest):
    execution_quantity: int = Field(default=1_000, ge=4)


class RiskSnapshotRequest(DataRequest):
    risk_capital: float = Field(default=1_000_000, gt=0)
    risk_quantity: int = Field(default=10, ge=1)
    commission: float = Field(default=0.001, ge=0)
    max_position_pct: float = Field(default=0.35, ge=0.01, le=10)
    max_leverage: float = Field(default=1.0, ge=0.01, le=100)
    max_drawdown_pct: float = Field(default=0.20, ge=0.01, le=1)
    max_var_pct: float = Field(default=0.03, ge=0.001, le=1)


class MarketOverviewRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    period: Literal["1mo", "3mo", "6mo", "1y"] = "1mo"


app = FastAPI(
    title="Quant Execution Lab",
    description="Validated market-data, research, portfolio, and execution simulation API.",
    version="0.4.0",
    docs_url="/api/docs",
    redoc_url=None,
    openapi_url="/api/openapi.json",
)


async def _operation_error(_: Request, exc: Exception) -> JSONResponse:
    return JSONResponse(status_code=400, content={"error": str(exc)})


for handled_exception in (
    ValueError,
    TypeError,
    OSError,
    RuntimeError,
    subprocess.TimeoutExpired,
):
    app.add_exception_handler(handled_exception, _operation_error)


@app.get("/api/health")
def api_health() -> dict[str, object]:
    with _YAHOO_CACHE_LOCK:
        cache_entries = len(_YAHOO_CACHE)
    return {"status": "ok", "version": app.version, "market_cache_entries": cache_entries}


@app.post("/api/dataset")
def api_dataset(request: DataRequest) -> dict[str, Any]:
    return dataset_summary(request.model_dump())


@app.post("/api/market-overview")
def api_market_overview(request: MarketOverviewRequest) -> dict[str, Any]:
    return indian_market_overview(request.period)


@app.post("/api/risk-snapshot")
def api_risk_snapshot(request: RiskSnapshotRequest) -> dict[str, Any]:
    return run_risk_snapshot(request.model_dump())


@app.post("/api/backtest")
def api_backtest(request: BacktestRequest) -> dict[str, Any]:
    return run_backtest(request.model_dump())


@app.post("/api/walk-forward")
def api_walk_forward(request: ResearchRequest) -> dict[str, Any]:
    return run_walk_forward(request.model_dump())


@app.post("/api/portfolio")
def api_portfolio(request: PortfolioRequest) -> dict[str, Any]:
    return run_portfolio_snapshot(request.model_dump())


@app.post("/api/execution")
def api_execution(request: ExecutionRequest) -> dict[str, Any]:
    return run_execution_simulation(request.model_dump())


app.mount("/", StaticFiles(directory=WEB_ROOT, html=True), name="dashboard")


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--workers", type=int, default=1)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    print(f"Quant dashboard running at http://{args.host}:{args.port}")
    uvicorn.run(
        "quant_system.dashboard:app",
        host=args.host,
        port=args.port,
        workers=args.workers,
        server_header=False,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
