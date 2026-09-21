import pytest

from quant_system.risk import RiskConfigurationError, RiskLimits


def test_risk_limits_accept_absolute_and_percentage_values() -> None:
    limits = RiskLimits(
        max_gross_exposure=100_000,
        max_net_exposure=50_000,
        max_position_notional=25_000,
        max_gross_exposure_pct=1.5,
        max_position_pct=0.5,
    )

    assert limits.max_gross_exposure == 100_000.0
    assert limits.max_position_pct == 0.5


@pytest.mark.parametrize("value", [-1.0, float("nan"), float("inf")])
def test_risk_limits_reject_invalid_values(value: float) -> None:
    with pytest.raises(RiskConfigurationError):
        RiskLimits(max_gross_exposure=value)


def test_zero_limit_is_valid_and_blocks_new_exposure() -> None:
    assert RiskLimits(max_gross_exposure=0.0).max_gross_exposure == 0.0

