"""Orchestration for the deterministic event-driven backtest pipeline."""

import logging
import math
from dataclasses import dataclass

import pandas as pd

from quant_system.backtest.events import (
    EventMessage,
    EventQueue,
    FillEvent,
    MarketEvent,
    OrderEvent,
    SignalEvent,
)
from quant_system.backtest.orders import FixedSizeOrderGenerator
from quant_system.data.models import CLOSE, HIGH, LOW, OPEN, SYMBOL, TIMESTAMP, VOLUME
from quant_system.data.validation import validate_ohlcv
from quant_system.execution.costs import CommissionModel
from quant_system.execution.simulator import ExecutionSimulator
from quant_system.execution.slippage import SlippageModel
from quant_system.portfolio.models import PortfolioSnapshot
from quant_system.portfolio.portfolio import Portfolio
from quant_system.quant.metrics import (
    annualized_volatility,
    cagr,
    max_drawdown,
    sharpe_ratio,
    sortino_ratio,
)
from quant_system.risk.manager import RejectedOrder, RiskManager, RiskRejectReason
from quant_system.strategy.base import Strategy

logger = logging.getLogger(__name__)

type PerformanceValue = float | int | None


@dataclass(slots=True)
class BacktestResult:
    """Lightweight output and audit trail from a completed backtest."""

    initial_capital: float
    final_equity: float
    total_return: float
    equity_curve: pd.Series
    fills: list[FillEvent]
    signals: list[SignalEvent]
    orders: list[OrderEvent]
    pending_orders: list[OrderEvent]
    rejected_orders: list[RejectedOrder]
    portfolio_snapshots: list[PortfolioSnapshot]
    portfolio: Portfolio
    events: list[EventMessage]
    performance: dict[str, PerformanceValue]

    @property
    def trades(self) -> list[FillEvent]:
        """Backward-compatible alias for executed fills."""

        return self.fills


class BacktestEngine:
    """Orchestrate strategy, risk, execution, and portfolio dependencies."""

    def __init__(
        self,
        data: pd.DataFrame,
        strategy: Strategy,
        *,
        initial_capital: float = 100_000.0,
        quantity: int = 100,
        execution_simulator: ExecutionSimulator | None = None,
        commission: CommissionModel | None = None,
        slippage: SlippageModel | None = None,
        risk_free_rate: float = 0.0,
        periods_per_year: int = 252,
        allow_short_selling: bool = False,
        risk_manager: RiskManager | None = None,
        warmup_data: pd.DataFrame | None = None,
    ) -> None:
        validate_ohlcv(data)
        if warmup_data is not None:
            validate_ohlcv(warmup_data)
            if pd.Timestamp(warmup_data[TIMESTAMP].max()) >= pd.Timestamp(data[TIMESTAMP].min()):
                raise ValueError("warmup_data must end before evaluation data begins")
        if execution_simulator is not None and (commission is not None or slippage is not None):
            raise ValueError(
                "pass either execution_simulator or commission/slippage models, not both"
            )
        if isinstance(periods_per_year, bool) or not isinstance(periods_per_year, int):
            raise TypeError("periods_per_year must be an integer")
        if periods_per_year <= 0:
            raise ValueError("periods_per_year must be positive")
        if isinstance(initial_capital, bool) or not isinstance(initial_capital, int | float):
            raise TypeError("initial_capital must be a real number")
        if not math.isfinite(float(initial_capital)) or initial_capital <= 0.0:
            raise ValueError("initial_capital must be finite and greater than zero")
        if isinstance(risk_free_rate, bool) or not isinstance(risk_free_rate, int | float):
            raise TypeError("risk_free_rate must be a real number")
        if not math.isfinite(float(risk_free_rate)) or risk_free_rate <= -1.0:
            raise ValueError("risk_free_rate must be finite and greater than -1.0")
        if not isinstance(allow_short_selling, bool):
            raise TypeError("allow_short_selling must be a bool")

        self.data = data.copy(deep=True)
        self.warmup_data = None if warmup_data is None else warmup_data.copy(deep=True)
        self.strategy = strategy
        self.initial_capital = float(initial_capital)
        self.order_generator = FixedSizeOrderGenerator(quantity)
        self.execution = execution_simulator or ExecutionSimulator(
            commission=commission,
            slippage=slippage,
        )
        self.risk_free_rate = risk_free_rate
        self.periods_per_year = periods_per_year
        self.allow_short_selling = allow_short_selling
        self.risk_manager = risk_manager or RiskManager()
        self._queue = EventQueue()

    def run(self) -> BacktestResult:
        """Run from a clean state, yielding identical output for identical input."""

        self._reset_components()
        portfolio = Portfolio(
            self.initial_capital,
            allow_short_selling=self.allow_short_selling,
        )
        signals: list[SignalEvent] = []
        orders: list[OrderEvent] = []
        fills: list[FillEvent] = []
        rejected_orders: list[RejectedOrder] = []
        event_trace: list[EventMessage] = []

        if self.warmup_data is not None:
            for market in self._market_events(self.warmup_data):
                self.strategy.warm_up(market)

        for market in self._market_events():
            self._queue.put(market)
            queued_market = self._queue.get()
            if not isinstance(queued_market, MarketEvent):
                raise RuntimeError("internal event ordering error: expected MarketEvent")
            event_trace.append(queued_market)
            logger.debug(
                "MARKET %s %s close=%s",
                market.symbol,
                market.timestamp.isoformat(),
                market.close,
            )

            self._process_pending_orders(
                market,
                portfolio,
                fills,
                rejected_orders,
                event_trace,
            )
            portfolio.mark_to_market(market)
            self._process_strategy(
                market,
                portfolio,
                signals,
                orders,
                rejected_orders,
                event_trace,
            )

        equity = portfolio.equity_curve
        final_equity = float(equity.iloc[-1])
        total_return = final_equity / self.initial_capital - 1.0
        performance = self._performance(equity, final_equity, total_return)
        return BacktestResult(
            initial_capital=self.initial_capital,
            final_equity=final_equity,
            total_return=total_return,
            equity_curve=equity,
            fills=fills,
            signals=signals,
            orders=orders,
            pending_orders=list(self.execution.pending_orders),
            rejected_orders=rejected_orders,
            portfolio_snapshots=list(portfolio.snapshots),
            portfolio=portfolio,
            events=event_trace,
            performance=performance,
        )

    def _reset_components(self) -> None:
        self._queue.clear()
        self.order_generator.reset()
        self.execution.reset()
        self.strategy.reset()

    def _market_events(self, data: pd.DataFrame | None = None) -> list[MarketEvent]:
        events: list[MarketEvent] = []
        source = self.data if data is None else data
        for _, row in source.iterrows():
            timestamp = pd.Timestamp(row[TIMESTAMP]).to_pydatetime()
            events.append(
                MarketEvent(
                    timestamp=timestamp,
                    symbol=str(row[SYMBOL]),
                    open=float(row[OPEN]),
                    high=float(row[HIGH]),
                    low=float(row[LOW]),
                    close=float(row[CLOSE]),
                    volume=float(row[VOLUME]),
                )
            )
        return events

    def _process_pending_orders(
        self,
        market: MarketEvent,
        portfolio: Portfolio,
        fills: list[FillEvent],
        rejected_orders: list[RejectedOrder],
        event_trace: list[EventMessage],
    ) -> None:
        rejected_before = len(self.execution.rejected_orders)
        for fill in self.execution.execute_pending(
            market,
            accept_fill=portfolio.can_process_fill,
        ):
            self._queue.put(fill)
        newly_rejected = self.execution.rejected_orders[rejected_before:]
        for order in newly_rejected:
            rejected_orders.append(
                RejectedOrder(
                    order=order,
                    timestamp=market.timestamp,
                    reason=RiskRejectReason.INSUFFICIENT_CASH,
                    message="next-bar fill price and commission exceeded available cash",
                )
            )
        while self._queue:
            event = self._queue.get()
            if not isinstance(event, FillEvent):
                raise RuntimeError("internal event ordering error: expected FillEvent")
            portfolio.process_fill(event)
            fills.append(event)
            event_trace.append(event)
            logger.debug(
                "FILL %s %s %d %s @ %s",
                event.order_id,
                event.side.value,
                event.quantity,
                event.symbol,
                event.fill_price,
            )

    def _process_strategy(
        self,
        market: MarketEvent,
        portfolio: Portfolio,
        signals: list[SignalEvent],
        orders: list[OrderEvent],
        rejected_orders: list[RejectedOrder],
        event_trace: list[EventMessage],
    ) -> None:
        generated_signals = self.strategy.on_market(market)
        for signal in generated_signals:
            if signal.timestamp != market.timestamp:
                raise ValueError("strategy signals must use the current market-event timestamp")
            self._queue.put(signal)

        while self._queue:
            event = self._queue.get()
            if isinstance(event, SignalEvent):
                signals.append(event)
                event_trace.append(event)
                logger.debug("SIGNAL %s %s", event.side.value, event.symbol)
                order = self.order_generator.generate(event, portfolio.position(event.symbol))
                if order is not None:
                    self._queue.put(order)
            elif isinstance(event, OrderEvent):
                orders.append(event)
                event_trace.append(event)
                decision = self.risk_manager.validate_order(
                    event,
                    portfolio,
                    market.close,
                )
                if decision.approved:
                    self.execution.submit(event)
                else:
                    if decision.reason is None or decision.message is None:
                        raise RuntimeError("rejected risk decision requires reason and message")
                    rejected_orders.append(
                        RejectedOrder(
                            order=event,
                            timestamp=market.timestamp,
                            reason=decision.reason,
                            message=decision.message,
                        )
                    )
                logger.debug(
                    "ORDER %s %d %s approved=%s",
                    event.side.value,
                    event.quantity,
                    event.symbol,
                    decision.approved,
                )
            else:
                raise RuntimeError("internal event ordering error in strategy processing")

    def _performance(
        self,
        equity: pd.Series,
        final_equity: float,
        total_return: float,
    ) -> dict[str, PerformanceValue]:
        baseline = pd.Series(
            [self.initial_capital],
            index=pd.DatetimeIndex([equity.index[0]], name="timestamp"),
            dtype=float,
        )
        equity_with_baseline = pd.concat([baseline, equity])
        returns = equity_with_baseline.pct_change(fill_method=None)

        def optional_metric(metric_name: str) -> float | None:
            try:
                if metric_name == "cagr":
                    return cagr(equity_with_baseline, self.periods_per_year)
                if metric_name == "annualized_volatility":
                    return annualized_volatility(returns, self.periods_per_year)
                if metric_name == "sharpe_ratio":
                    return sharpe_ratio(
                        returns,
                        risk_free_rate=self.risk_free_rate,
                        periods_per_year=self.periods_per_year,
                    )
                if metric_name == "sortino_ratio":
                    return sortino_ratio(
                        returns,
                        risk_free_rate=self.risk_free_rate,
                        periods_per_year=self.periods_per_year,
                    )
            except ValueError:
                return None
            raise ValueError(f"unknown metric '{metric_name}'")

        return {
            "total_return": total_return,
            "cagr": optional_metric("cagr"),
            "annualized_volatility": optional_metric("annualized_volatility"),
            "sharpe_ratio": optional_metric("sharpe_ratio"),
            "sortino_ratio": optional_metric("sortino_ratio"),
            "max_drawdown": max_drawdown(equity_with_baseline),
            "ending_equity": final_equity,
            "observations": len(equity),
        }
