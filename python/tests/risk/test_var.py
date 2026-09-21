import pandas as pd
import pytest

from quant_system.risk import expected_shortfall, historical_var, parametric_var, var_amount


def tail_returns() -> pd.Series:
    return pd.Series([-0.10, -0.05, -0.02, 0.00, 0.01, 0.02, 0.03, 0.04, 0.05, 0.06])


def test_historical_var_is_positive_empirical_loss_magnitude() -> None:
    # At 90% confidence, NumPy's documented linear rank is 0.9 between
    # -10% and -5%, producing a -5.5% cutoff.
    assert historical_var(tail_returns(), confidence=0.90) == pytest.approx(0.055)


def test_parametric_var_uses_gaussian_lower_tail_and_sample_std() -> None:
    returns = pd.Series([-0.02, 0.0, 0.02])

    # Mean=0, sample std=2%, and the 95% lower-tail z is -1.6448536269.
    assert parametric_var(returns, confidence=0.95) == pytest.approx(0.0328970725)


def test_higher_confidence_has_no_lower_var_for_deterministic_tail() -> None:
    assert historical_var(tail_returns(), 0.99) >= historical_var(tail_returns(), 0.95)


def test_expected_shortfall_is_average_loss_beyond_var_cutoff() -> None:
    value_at_risk = historical_var(tail_returns(), confidence=0.90)
    shortfall = expected_shortfall(tail_returns(), confidence=0.90)

    assert shortfall == pytest.approx(0.10)
    assert shortfall >= value_at_risk


def test_var_amount_converts_fraction_to_money() -> None:
    assert var_amount(1_000_000.0, 0.02) == pytest.approx(20_000.0)


@pytest.mark.parametrize("confidence", [0.0, 1.0, -0.1, 1.1])
def test_var_rejects_invalid_confidence(confidence: float) -> None:
    with pytest.raises(ValueError, match="confidence"):
        historical_var(tail_returns(), confidence)


def test_var_rejects_nan_and_non_numeric_data() -> None:
    with pytest.raises(ValueError):
        historical_var(pd.Series([0.01, float("nan")]))
    with pytest.raises(ValueError):
        historical_var(pd.Series(["bad", "data"]))


def test_var_amount_rejects_negative_values() -> None:
    with pytest.raises(ValueError):
        var_amount(-1.0, 0.02)
    with pytest.raises(ValueError):
        var_amount(100.0, -0.02)
