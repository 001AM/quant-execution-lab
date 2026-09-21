from datetime import datetime

from quant_system.data import OHLCVBar


def test_ohlcv_bar_is_a_typed_immutable_value() -> None:
    bar = OHLCVBar(
        timestamp=datetime(2026, 1, 5, 9, 30),
        symbol="AAPL",
        open=100.0,
        high=102.0,
        low=99.0,
        close=101.0,
        volume=1_000.0,
    )

    assert bar.symbol == "AAPL"
    assert bar.high == 102.0

