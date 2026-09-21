import pytest

from quant_system.execution import FixedCommission, PercentageCommission, ZeroCommission


def test_zero_commission() -> None:
    assert ZeroCommission().calculate(100, 100.0) == 0.0


def test_fixed_commission_is_per_order() -> None:
    model = FixedCommission(5.0)

    assert model.calculate(100, 100.0) == 5.0
    assert model.calculate(1_000, 200.0) == 5.0


def test_percentage_commission_uses_execution_notional() -> None:
    model = PercentageCommission(0.001)

    assert model.calculate(100, 100.10) == pytest.approx(10.01)


@pytest.mark.parametrize("value", [-0.01, float("nan"), float("inf")])
def test_commission_parameters_must_be_finite_and_non_negative(value: float) -> None:
    with pytest.raises(ValueError):
        FixedCommission(value)
    with pytest.raises(ValueError):
        PercentageCommission(value)

