from pathlib import Path

import pandas as pd
import pytest

from quant_system.data import (
    MarketDataLoader,
    MarketDataSchemaError,
    MarketDataValidationError,
)
from quant_system.data.models import CANONICAL_COLUMNS


@pytest.fixture
def loader() -> MarketDataLoader:
    return MarketDataLoader()


def write_csv(tmp_path: Path, body: str) -> Path:
    path = tmp_path / "prices.csv"
    path.write_text(body, encoding="utf-8")
    return path


def valid_csv(*, header: str = "Date,Open,High,Low,Close,Volume") -> str:
    return (
        f"{header}\n"
        "2026-01-06,101,103,100,102,1200\n"
        "2026-01-05,100,102,99,101,1000\n"
    )


def test_valid_csv_loads_with_canonical_schema(
    loader: MarketDataLoader, tmp_path: Path
) -> None:
    frame = loader.load_csv(write_csv(tmp_path, valid_csv()), symbol="AAPL")

    assert tuple(frame.columns) == CANONICAL_COLUMNS
    assert len(frame) == 2
    assert frame["symbol"].tolist() == ["AAPL", "AAPL"]
    assert all(pd.api.types.is_float_dtype(frame[column]) for column in CANONICAL_COLUMNS[2:])


def test_checked_in_sample_loads(loader: MarketDataLoader) -> None:
    repository_root = Path(__file__).parents[3]

    frame = loader.load_csv(repository_root / "data/sample/AAPL.csv", symbol="AAPL")

    assert len(frame) == 30
    assert frame["timestamp"].is_monotonic_increasing
    assert frame["close"].iloc[-1] == 112.45


def test_api_records_use_the_canonical_validation_pipeline(
    loader: MarketDataLoader,
) -> None:
    records = [
        {
            "Datetime": "2026-01-06 09:30:00",
            "Open": "101",
            "High": "103",
            "Low": "100",
            "Close": "102",
            "Volume": "1200",
        },
        {
            "Datetime": "2026-01-05 09:30:00",
            "Open": "100",
            "High": "102",
            "Low": "99",
            "Close": "101",
            "Volume": "1000",
        },
    ]

    frame = loader.load_records(records, symbol="AAPL")

    assert tuple(frame.columns) == CANONICAL_COLUMNS
    assert frame["timestamp"].is_monotonic_increasing
    assert frame["close"].tolist() == [101.0, 102.0]


def test_frame_adapter_does_not_mutate_provider_data(loader: MarketDataLoader) -> None:
    provider_frame = pd.DataFrame(
        {
            "Date": ["2026-01-05"],
            "Open": [100],
            "High": [102],
            "Low": [99],
            "Close": [101],
            "Volume": [1000],
        }
    )
    original = provider_frame.copy(deep=True)

    normalized = loader.load_frame(provider_frame, symbol="AAPL")

    pd.testing.assert_frame_equal(provider_frame, original)
    assert tuple(normalized.columns) == CANONICAL_COLUMNS


def test_api_record_corruption_fails_loudly(loader: MarketDataLoader) -> None:
    records = [
        {
            "Date": "2026-01-05",
            "Open": 100,
            "High": 98,
            "Low": 99,
            "Close": 101,
            "Volume": 1000,
        }
    ]

    with pytest.raises(MarketDataValidationError, match=r"high=98\.0 cannot be lower"):
        loader.load_records(records, symbol="AAPL")


def test_timestamps_are_parsed(loader: MarketDataLoader, tmp_path: Path) -> None:
    frame = loader.load_csv(write_csv(tmp_path, valid_csv()), symbol="AAPL")

    assert pd.api.types.is_datetime64_any_dtype(frame["timestamp"])
    assert frame.loc[0, "timestamp"] == pd.Timestamp("2026-01-05")


def test_rows_are_sorted_chronologically(loader: MarketDataLoader, tmp_path: Path) -> None:
    frame = loader.load_csv(write_csv(tmp_path, valid_csv()), symbol="AAPL")

    assert frame["timestamp"].is_monotonic_increasing
    assert frame["close"].tolist() == [101.0, 102.0]


def test_uppercase_columns_normalize(loader: MarketDataLoader, tmp_path: Path) -> None:
    csv = valid_csv(header="TIMESTAMP,OPEN,HIGH,LOW,CLOSE,VOLUME")

    frame = loader.load_csv(write_csv(tmp_path, csv), symbol="AAPL")

    assert tuple(frame.columns) == CANONICAL_COLUMNS


def test_duplicate_timestamps_for_symbol_fail(
    loader: MarketDataLoader, tmp_path: Path
) -> None:
    csv = (
        "Date,Open,High,Low,Close,Volume\n"
        "2026-01-05,100,102,99,101,1000\n"
        "2026-01-05,101,103,100,102,1200\n"
    )

    with pytest.raises(MarketDataValidationError, match=r"duplicate \(symbol, timestamp\)"):
        loader.load_csv(write_csv(tmp_path, csv), symbol="AAPL")


def test_missing_required_column_fails(loader: MarketDataLoader, tmp_path: Path) -> None:
    csv = "Date,Open,High,Close,Volume\n2026-01-05,100,102,101,1000\n"

    with pytest.raises(MarketDataSchemaError, match="low"):
        loader.load_csv(write_csv(tmp_path, csv), symbol="AAPL")


@pytest.mark.parametrize("bad_value", ["not-a-number", "--"])
def test_invalid_numeric_value_fails(
    loader: MarketDataLoader, tmp_path: Path, bad_value: str
) -> None:
    csv = (
        "Date,Open,High,Low,Close,Volume\n"
        f"2026-01-05,{bad_value},102,99,101,1000\n"
    )

    with pytest.raises(MarketDataValidationError, match="Invalid numeric value for 'open'"):
        loader.load_csv(write_csv(tmp_path, csv), symbol="AAPL")


def test_nan_value_fails(loader: MarketDataLoader, tmp_path: Path) -> None:
    csv = "Date,Open,High,Low,Close,Volume\n2026-01-05,,102,99,101,1000\n"

    with pytest.raises(MarketDataValidationError, match="Invalid numeric value for 'open'"):
        loader.load_csv(write_csv(tmp_path, csv), symbol="AAPL")


@pytest.mark.parametrize("infinity", ["inf", "-inf", "Infinity"])
def test_infinity_fails(
    loader: MarketDataLoader, tmp_path: Path, infinity: str
) -> None:
    csv = (
        "Date,Open,High,Low,Close,Volume\n"
        f"2026-01-05,100,{infinity},99,101,1000\n"
    )

    with pytest.raises(MarketDataValidationError, match=r"high=.* is not finite"):
        loader.load_csv(write_csv(tmp_path, csv), symbol="AAPL")


def test_negative_volume_fails(loader: MarketDataLoader, tmp_path: Path) -> None:
    csv = "Date,Open,High,Low,Close,Volume\n2026-01-05,100,102,99,101,-1\n"

    with pytest.raises(MarketDataValidationError, match=r"volume=-1\.0 cannot be negative"):
        loader.load_csv(write_csv(tmp_path, csv), symbol="AAPL")


def test_high_below_low_fails(loader: MarketDataLoader, tmp_path: Path) -> None:
    csv = "Date,Open,High,Low,Close,Volume\n2026-01-05,100,98,99,99,1000\n"

    with pytest.raises(
        MarketDataValidationError,
        match=r"high=98\.0 cannot be lower than low=99\.0",
    ):
        loader.load_csv(write_csv(tmp_path, csv), symbol="AAPL")


def test_high_below_open_fails(loader: MarketDataLoader, tmp_path: Path) -> None:
    csv = "Date,Open,High,Low,Close,Volume\n2026-01-05,100,99,98,99,1000\n"

    with pytest.raises(
        MarketDataValidationError,
        match=r"high=99\.0 cannot be lower than open=100\.0",
    ):
        loader.load_csv(write_csv(tmp_path, csv), symbol="AAPL")


def test_low_above_close_fails(loader: MarketDataLoader, tmp_path: Path) -> None:
    csv = "Date,Open,High,Low,Close,Volume\n2026-01-05,102,103,101,100,1000\n"

    with pytest.raises(
        MarketDataValidationError,
        match=r"low=101\.0 cannot be higher than close=100\.0",
    ):
        loader.load_csv(write_csv(tmp_path, csv), symbol="AAPL")


def test_custom_column_mapping(loader: MarketDataLoader, tmp_path: Path) -> None:
    csv = (
        "Trading Day,First,Maximum,Minimum,Last,Shares\n"
        "2026-01-05,100,102,99,101,1000\n"
    )
    mapping = {
        "Trading Day": "timestamp",
        "First": "open",
        "Maximum": "high",
        "Minimum": "low",
        "Last": "close",
        "Shares": "volume",
    }

    frame = loader.load_csv(
        write_csv(tmp_path, csv), symbol="AAPL", column_mapping=mapping
    )

    assert frame.loc[0, "close"] == 101.0


def test_multiple_symbols_are_safe(loader: MarketDataLoader, tmp_path: Path) -> None:
    csv = (
        "Timestamp,Symbol,Open,High,Low,Close,Volume\n"
        "2026-01-05 09:31:00,MSFT,200,202,199,201,2000\n"
        "2026-01-05 09:30:00,AAPL,100,102,99,101,1000\n"
        "2026-01-05 09:30:00,MSFT,199,201,198,200,1800\n"
    )

    frame = loader.load_csv(write_csv(tmp_path, csv))

    assert frame["symbol"].tolist() == ["AAPL", "MSFT", "MSFT"]
    assert not frame.duplicated(subset=["symbol", "timestamp"]).any()


def test_close_takes_precedence_over_adjusted_close(
    loader: MarketDataLoader, tmp_path: Path
) -> None:
    csv = (
        "Date,Open,High,Low,Adj Close,Close,Volume\n"
        "2026-01-05,100,102,99,77,101,1000\n"
    )

    frame = loader.load_csv(write_csv(tmp_path, csv), symbol="AAPL")

    assert frame.loc[0, "close"] == 101.0


def test_invalid_timestamp_fails(loader: MarketDataLoader, tmp_path: Path) -> None:
    csv = "Date,Open,High,Low,Close,Volume\nnot-a-date,100,102,99,101,1000\n"

    with pytest.raises(MarketDataValidationError, match="Invalid timestamp"):
        loader.load_csv(write_csv(tmp_path, csv), symbol="AAPL")


def test_symbol_argument_cannot_overwrite_different_csv_symbol(
    loader: MarketDataLoader, tmp_path: Path
) -> None:
    csv = (
        "Date,Symbol,Open,High,Low,Close,Volume\n"
        "2026-01-05,MSFT,100,102,99,101,1000\n"
    )

    with pytest.raises(MarketDataValidationError, match="conflicts"):
        loader.load_csv(write_csv(tmp_path, csv), symbol="AAPL")


def test_missing_symbol_value_fails(loader: MarketDataLoader, tmp_path: Path) -> None:
    csv = (
        "Date,Symbol,Open,High,Low,Close,Volume\n"
        "2026-01-05,,100,102,99,101,1000\n"
    )

    with pytest.raises(MarketDataValidationError, match="missing value in 'symbol'"):
        loader.load_csv(write_csv(tmp_path, csv))
