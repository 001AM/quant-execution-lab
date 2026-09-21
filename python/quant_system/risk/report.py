"""Consolidated statistical risk reporting."""

from dataclasses import dataclass

import pandas as pd

from quant_system.portfolio.portfolio import Portfolio
from quant_system.quant.metrics import annualized_volatility, max_drawdown
from quant_system.risk.analytics import alpha, beta, max_drawdown_period
from quant_system.risk.var import expected_shortfall, historical_var, parametric_var


@dataclass(frozen=True, slots=True)
class RiskReport:
    annualized_volatility: float
    beta: float | None
    alpha: float | None
    historical_var_95: float
    historical_var_99: float
    parametric_var_95: float
    expected_shortfall_95: float
    max_drawdown: float
    max_drawdown_duration: int
    current_gross_exposure: float | None = None
    current_net_exposure: float | None = None
    current_leverage: float | None = None

    def to_text(self) -> str:
        beta_text = "N/A" if self.beta is None else f"{self.beta:.4f}"
        alpha_text = "N/A" if self.alpha is None else f"{self.alpha:.2%}"
        return "\n".join(
            (
                "RISK REPORT",
                "----------------------------------",
                f"Annualized Volatility  {self.annualized_volatility:.2%}",
                f"Beta                   {beta_text}",
                f"Alpha                  {alpha_text}",
                f"Historical VaR 95%     {self.historical_var_95:.2%}",
                f"Historical VaR 99%     {self.historical_var_99:.2%}",
                f"Parametric VaR 95%     {self.parametric_var_95:.2%}",
                f"Expected Shortfall 95% {self.expected_shortfall_95:.2%}",
                f"Maximum Drawdown       {self.max_drawdown:.2%}",
                f"Max Underwater Period  {self.max_drawdown_duration} bars",
            )
        )


def generate_risk_report(
    returns: pd.Series,
    equity: pd.Series,
    benchmark_returns: pd.Series | None = None,
    *,
    risk_free_rate: float = 0.0,
    periods_per_year: int = 252,
    portfolio: Portfolio | None = None,
) -> RiskReport:
    """Build a risk report from observed returns without fabricating a benchmark."""

    beta_value = None if benchmark_returns is None else beta(returns, benchmark_returns)
    alpha_value = (
        None
        if benchmark_returns is None
        else alpha(
            returns,
            benchmark_returns,
            risk_free_rate=risk_free_rate,
            periods_per_year=periods_per_year,
        )
    )
    deepest = max_drawdown_period(equity)
    gross_exposure = None if portfolio is None else portfolio.gross_exposure
    net_exposure = None if portfolio is None else portfolio.net_exposure
    leverage = None if portfolio is None else portfolio.gross_exposure_pct
    return RiskReport(
        annualized_volatility=annualized_volatility(returns, periods_per_year),
        beta=beta_value,
        alpha=alpha_value,
        historical_var_95=historical_var(returns, 0.95),
        historical_var_99=historical_var(returns, 0.99),
        parametric_var_95=parametric_var(returns, 0.95),
        expected_shortfall_95=expected_shortfall(returns, 0.95),
        max_drawdown=max_drawdown(equity),
        max_drawdown_duration=0 if deepest is None else deepest.duration,
        current_gross_exposure=gross_exposure,
        current_net_exposure=net_exposure,
        current_leverage=leverage,
    )

