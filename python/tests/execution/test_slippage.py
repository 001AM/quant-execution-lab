import pytest

from quant_system.backtest import OrderSide
from quant_system.execution import FixedSlippage, NoSlippage, PercentageSlippage


def test_no_slippage_preserves_price() -> None:
    model = NoSlippage()

    assert model.apply(100.0, OrderSide.BUY) == 100.0
    assert model.apply(100.0, OrderSide.SELL) == 100.0


def test_fixed_slippage_always_worsens_price() -> None:
    model = FixedSlippage(0.25)

    assert model.apply(100.0, OrderSide.BUY) == 100.25
    assert model.apply(100.0, OrderSide.SELL) == 99.75


def test_percentage_slippage_always_worsens_price() -> None:
    model = PercentageSlippage(0.001)

    assert model.apply(100.0, OrderSide.BUY) == pytest.approx(100.10)
    assert model.apply(100.0, OrderSide.SELL) == pytest.approx(99.90)


@pytest.mark.parametrize("rate", [-0.1, 1.0, float("nan"), float("inf")])
def test_percentage_slippage_rejects_invalid_rate(rate: float) -> None:
    with pytest.raises(ValueError):
        PercentageSlippage(rate)


def test_fixed_sell_slippage_cannot_create_non_positive_price() -> None:
    with pytest.raises(ValueError, match="non-positive"):
        FixedSlippage(101.0).apply(100.0, OrderSide.SELL)

