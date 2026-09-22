import asyncio
from pathlib import Path

import httpx
import pandas as pd
import pytest

import quant_system.dashboard as dashboard
from quant_system.dashboard import (
    WEB_ROOT,
    _currency_for_symbol,
    _normalize_yahoo_symbol,
    dataset_summary,
    indian_market_overview,
    run_backtest,
    run_portfolio_snapshot,
    run_risk_snapshot,
    run_walk_forward,
)


@pytest.fixture
def sample_payload() -> dict[str, object]:
    return {"source": "csv", "data_path": "data/sample/AAPL.csv", "symbol": "AAPL"}


def api_request(method: str, path: str, **kwargs: object) -> httpx.Response:
    async def send() -> httpx.Response:
        transport = httpx.ASGITransport(app=dashboard.app)
        async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
            return await client.request(method, path, **kwargs)

    return asyncio.run(send())


def test_dashboard_assets_are_packaged() -> None:
    assert {path.name for path in Path(WEB_ROOT).iterdir()} == {
        "app.js",
        "index.html",
        "styles.css",
    }


def test_production_api_serves_health_and_dashboard() -> None:
    health = api_request("GET", "/api/health")
    page = api_request("GET", "/")

    assert health.status_code == 200
    assert health.json()["status"] == "ok"
    assert health.json()["version"] == "0.4.1"
    assert page.status_code == 200
    assert "Quant Execution Lab" in page.text


def test_api_rejects_unknown_request_fields() -> None:
    response = api_request("POST", "/api/dataset", json={"symbol": "AAPL", "unexpected": True})

    assert response.status_code == 422


def test_dataset_summary_uses_real_sample_data(sample_payload: dict[str, object]) -> None:
    summary = dataset_summary(sample_payload)

    assert summary["symbol"] == "AAPL"
    assert summary["bars"] == 30
    assert summary["source"] == "data/sample/AAPL.csv"


def test_backtest_returns_each_strategy_and_equity(sample_payload: dict[str, object]) -> None:
    result = run_backtest({**sample_payload, "strategy": "all"})

    assert [run["strategy"] for run in result["runs"]] == [
        "momentum",
        "mean_reversion",
        "ma_crossover",
    ]
    assert all(len(run["equity"]) == 30 for run in result["runs"])


def test_walk_forward_returns_fold_results(sample_payload: dict[str, object]) -> None:
    result = run_walk_forward(sample_payload)

    assert len(result["folds"]) == 3
    assert result["performance"]["observations"] == 15


def test_portfolio_snapshot_uses_selected_market_data(
    sample_payload: dict[str, object],
) -> None:
    result = run_portfolio_snapshot(
        {**sample_payload, "portfolio_capital": 100_000, "portfolio_quantity": 10}
    )

    assert result["symbol"] == "AAPL"
    assert result["entry_price"] == 100.0
    assert result["market_price"] == 112.45
    assert result["equity"] == pytest.approx(100_123.5)
    assert len(result["positions"]) == 1


def test_risk_snapshot_calculates_limits_from_selected_market(
    sample_payload: dict[str, object],
) -> None:
    result = run_risk_snapshot(
        {
            **sample_payload,
            "risk_capital": 100_000,
            "risk_quantity": 10,
            "max_position_pct": 0.35,
            "max_leverage": 1.0,
            "max_drawdown_pct": 0.20,
            "max_var_pct": 0.03,
        }
    )

    assert result["symbol"] == "AAPL"
    assert result["position"]["quantity"] == 10
    assert result["gross_exposure"] == pytest.approx(1_124.5)
    assert result["equity"] == pytest.approx(100_123.5)
    assert len(result["limits"]) == 4
    assert result["status"] == "healthy"


def test_risk_snapshot_reports_real_limit_breach(sample_payload: dict[str, object]) -> None:
    result = run_risk_snapshot(
        {
            **sample_payload,
            "risk_capital": 100_000,
            "risk_quantity": 100,
            "max_position_pct": 0.01,
        }
    )

    assert result["status"] == "breach"
    assert result["risk_score"] > 100
    assert any(alert["severity"] == "breach" for alert in result["alerts"])


def test_dashboard_rejects_data_outside_project() -> None:
    with pytest.raises(ValueError, match="inside the project"):
        dataset_summary({"source": "csv", "data_path": "../../outside.csv", "symbol": "AAPL"})


def test_yahoo_source_is_normalized_for_dashboard(monkeypatch: pytest.MonkeyPatch) -> None:
    downloaded = pd.DataFrame(
        {
            "timestamp": pd.to_datetime(["2026-01-02", "2026-01-05", "2026-01-06"]),
            "symbol": ["MSFT"] * 3,
            "open": [100.0, 101.0, 103.0],
            "high": [102.0, 104.0, 105.0],
            "low": [99.0, 100.0, 102.0],
            "close": [101.0, 103.0, 104.0],
            "volume": [1_000.0, 1_200.0, 1_100.0],
        }
    )
    monkeypatch.setattr(dashboard, "_download_yahoo", lambda symbol, period: downloaded)

    summary = dataset_summary({"source": "yahoo", "symbol": "MSFT", "period": "3mo"})

    assert summary["provider"] == "yahoo"
    assert summary["exchange"] == "Global"
    assert summary["currency"] == "USD"
    assert summary["source"] == "Yahoo Finance · MSFT · 3mo"
    assert summary["bars"] == 3
    assert len(summary["prices"]) == 3


def test_indian_market_overview_builds_real_sector_baskets(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    def downloaded(symbol: str, period: str) -> pd.DataFrame:
        assert period == "1mo"
        symbol_offset = sum(ord(character) for character in symbol) % 8
        first = 100.0 + symbol_offset
        last = first + symbol_offset - 2
        return pd.DataFrame(
            {
                "timestamp": pd.to_datetime(["2026-01-02", "2026-01-30"]),
                "symbol": [symbol, symbol],
                "open": [first, last],
                "high": [first + 1, last + 1],
                "low": [first - 1, last - 1],
                "close": [first, last],
                "volume": [1_000.0, 1_200.0],
            }
        )

    monkeypatch.setattr(dashboard, "_download_yahoo", downloaded)

    overview = indian_market_overview("1mo")

    assert len(overview["indices"]) == 3
    assert len(overview["sectors"]) == 10
    assert overview["available_instruments"] == overview["requested_instruments"]
    assert all(sector["available"] == 3 for sector in overview["sectors"])
    assert overview["methodology"].startswith("Equal-weight")


@pytest.mark.parametrize(
    ("symbol", "exchange", "expected"),
    [
        ("RELIANCE", "nse", "RELIANCE.NS"),
        ("500325", "bse", "500325.BO"),
        ("NIFTY50", "auto", "^NSEI"),
        ("BANK NIFTY", "auto", "^NSEBANK"),
        ("SENSEX", "auto", "^BSESN"),
        ("TCS.NS", "auto", "TCS.NS"),
    ],
)
def test_indian_yahoo_symbols_are_normalized(symbol: str, exchange: str, expected: str) -> None:
    assert _normalize_yahoo_symbol(symbol, exchange) == expected
    assert _currency_for_symbol(expected) == "INR"
