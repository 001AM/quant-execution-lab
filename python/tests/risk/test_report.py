import pandas as pd
import pytest

from quant_system.risk import generate_risk_report


def test_risk_report_without_benchmark_does_not_fabricate_relative_metrics() -> None:
    returns = pd.Series([0.02, -0.01, -0.03, 0.01])
    equity = pd.Series([100.0, 102.0, 100.98, 97.9506, 98.930106])

    report = generate_risk_report(returns, equity, periods_per_year=4)

    assert report.beta is None
    assert report.alpha is None
    assert report.max_drawdown == pytest.approx(97.9506 / 102.0 - 1.0)
    assert report.max_drawdown_duration == 3
    assert report.expected_shortfall_95 >= report.historical_var_95
    assert "RISK REPORT" in report.to_text()


def test_risk_report_with_benchmark_calculates_beta_and_alpha() -> None:
    benchmark = pd.Series([0.01, 0.02, -0.01, 0.03])
    returns = 0.001 + 2.0 * benchmark
    equity = 100.0 * (1.0 + returns).cumprod()
    equity = pd.concat([pd.Series([100.0]), equity], ignore_index=True)

    report = generate_risk_report(
        returns,
        equity,
        benchmark,
        periods_per_year=252,
    )

    assert report.beta == pytest.approx(2.0)
    assert report.alpha == pytest.approx(0.252)
