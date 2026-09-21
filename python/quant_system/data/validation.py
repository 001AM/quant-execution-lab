"""Reusable validation for normalized OHLCV frames."""

from collections.abc import Sequence

import numpy as np
import pandas as pd

from quant_system.data.exceptions import (
    MarketDataSchemaError,
    MarketDataValidationError,
)
from quant_system.data.models import (
    CANONICAL_COLUMNS,
    CLOSE,
    HIGH,
    LOW,
    NUMERIC_COLUMNS,
    OPEN,
    SYMBOL,
    TIMESTAMP,
    VOLUME,
)


def _row_context(frame: pd.DataFrame, index: object) -> str:
    symbol = frame.at[index, SYMBOL]
    timestamp = frame.at[index, TIMESTAMP]
    return f"{symbol} {timestamp}"


def _first_true_index(mask: pd.Series) -> object:
    return mask.index[mask.to_numpy().nonzero()[0][0]]


def _require_columns(frame: pd.DataFrame, required: Sequence[str]) -> None:
    missing = [column for column in required if column not in frame.columns]
    if missing:
        joined = ", ".join(missing)
        raise MarketDataSchemaError(f"Missing required canonical columns: {joined}")


def validate_ohlcv(frame: pd.DataFrame) -> None:
    """Validate a normalized OHLCV frame, raising on the first bad invariant.

    The function does not mutate or repair the input. A valid frame may contain
    several symbols, but records must be globally chronological and each
    ``(symbol, timestamp)`` pair must be unique.
    """

    _require_columns(frame, CANONICAL_COLUMNS)
    if frame.empty:
        raise MarketDataValidationError("OHLCV data contains no rows")

    missing_mask = frame.loc[:, list(CANONICAL_COLUMNS)].isna()
    if missing_mask.to_numpy().any():
        row_position, column_position = np.argwhere(missing_mask.to_numpy())[0]
        index = frame.index[int(row_position)]
        column = CANONICAL_COLUMNS[int(column_position)]
        raise MarketDataValidationError(
            f"{_row_context(frame, index)}: missing value in '{column}'"
        )

    empty_symbol = frame[SYMBOL].astype(str).str.strip().eq("")
    if empty_symbol.any():
        index = _first_true_index(empty_symbol)
        raise MarketDataValidationError(f"{frame.at[index, TIMESTAMP]}: symbol is empty")

    if not pd.api.types.is_datetime64_any_dtype(frame[TIMESTAMP].dtype):
        raise MarketDataValidationError("timestamp must have a pandas datetime dtype")

    for column in NUMERIC_COLUMNS:
        if not pd.api.types.is_numeric_dtype(frame[column].dtype):
            raise MarketDataValidationError(f"'{column}' must contain numeric values")
        values = frame[column].to_numpy(dtype=float)
        finite_mask = pd.Series(np.isfinite(values), index=frame.index)
        if not finite_mask.all():
            index = _first_true_index(~finite_mask)
            value = frame.at[index, column]
            raise MarketDataValidationError(
                f"{_row_context(frame, index)}: {column}={value!r} is not finite"
            )

    duplicates = frame.duplicated(subset=[SYMBOL, TIMESTAMP], keep=False)
    if duplicates.any():
        index = _first_true_index(duplicates)
        raise MarketDataValidationError(
            f"{_row_context(frame, index)}: duplicate (symbol, timestamp) record"
        )

    if not frame[TIMESTAMP].is_monotonic_increasing:
        raise MarketDataValidationError("timestamps are not in chronological order")

    rules = (
        (frame[HIGH] < frame[LOW], HIGH, "cannot be lower than", LOW),
        (frame[HIGH] < frame[OPEN], HIGH, "cannot be lower than", OPEN),
        (frame[HIGH] < frame[CLOSE], HIGH, "cannot be lower than", CLOSE),
        (frame[LOW] > frame[OPEN], LOW, "cannot be higher than", OPEN),
        (frame[LOW] > frame[CLOSE], LOW, "cannot be higher than", CLOSE),
    )
    for mask, left, relation, right in rules:
        if mask.any():
            index = _first_true_index(mask)
            raise MarketDataValidationError(
                f"{_row_context(frame, index)}: "
                f"{left}={frame.at[index, left]} {relation} "
                f"{right}={frame.at[index, right]}"
            )

    negative_volume = frame[VOLUME] < 0
    if negative_volume.any():
        index = _first_true_index(negative_volume)
        raise MarketDataValidationError(
            f"{_row_context(frame, index)}: volume={frame.at[index, VOLUME]} "
            "cannot be negative"
        )

