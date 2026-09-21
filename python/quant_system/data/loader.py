"""External-data ingestion into the canonical OHLCV representation."""

from collections.abc import Iterable, Mapping
from os import PathLike
from pathlib import Path

import pandas as pd

from quant_system.data.exceptions import (
    MarketDataError,
    MarketDataSchemaError,
    MarketDataValidationError,
)
from quant_system.data.models import (
    CANONICAL_COLUMNS,
    NUMERIC_COLUMNS,
    SYMBOL,
    TIMESTAMP,
)
from quant_system.data.validation import validate_ohlcv

_COLUMN_ALIASES: dict[str, tuple[str, ...]] = {
    TIMESTAMP: ("timestamp", "datetime", "date", "time"),
    SYMBOL: ("symbol", "ticker"),
    "open": ("open",),
    "high": ("high",),
    "low": ("low",),
    # Adjusted close is intentionally lower priority than an actual close.
    "close": ("close", "adjclose", "adjustedclose"),
    "volume": ("volume", "vol"),
}


def _column_key(name: object) -> str:
    return "".join(character for character in str(name).casefold() if character.isalnum())


class MarketDataLoader:
    """Load external market data and normalize it to the canonical schema."""

    def load_csv(
        self,
        path: str | PathLike[str],
        *,
        symbol: str | None = None,
        column_mapping: Mapping[str, str] | None = None,
    ) -> pd.DataFrame:
        """Load and validate an OHLCV CSV file.

        ``column_mapping`` maps source column names to canonical names, for
        example ``{"Trading Day": "timestamp"}``. If ``symbol`` is supplied,
        it fills a missing symbol column and must agree with any existing values.
        Invalid financial values are rejected rather than repaired.
        """

        source = Path(path)
        try:
            raw = pd.read_csv(source, dtype=str)
        except (OSError, pd.errors.ParserError, pd.errors.EmptyDataError) as exc:
            raise MarketDataError(f"Unable to read market-data CSV '{source}': {exc}") from exc

        return self._normalize_and_validate(
            raw,
            symbol=symbol,
            column_mapping=column_mapping,
            row_label="CSV row",
            row_offset=2,
        )

    def load_frame(
        self,
        frame: pd.DataFrame,
        *,
        symbol: str | None = None,
        column_mapping: Mapping[str, str] | None = None,
    ) -> pd.DataFrame:
        """Normalize and validate an in-memory vendor/API DataFrame.

        The input is copied and never mutated. This is the adapter boundary for
        API clients that already return tabular data; it applies the same column
        mapping, parsing, sorting, and validation rules as :meth:`load_csv`.
        """

        if not isinstance(frame, pd.DataFrame):
            raise MarketDataError("Market-data input must be a pandas DataFrame")
        return self._normalize_and_validate(
            frame.copy(deep=True),
            symbol=symbol,
            column_mapping=column_mapping,
            row_label="frame row",
            row_offset=0,
        )

    def load_records(
        self,
        records: Iterable[Mapping[str, object]],
        *,
        symbol: str | None = None,
        column_mapping: Mapping[str, str] | None = None,
    ) -> pd.DataFrame:
        """Normalize records returned by a market-data API.

        Network access and provider authentication intentionally remain outside
        the loader. Callers pass decoded records so corrupt external values still
        cross the same strict canonical-schema boundary as CSV input.
        """

        try:
            raw = pd.DataFrame.from_records(list(records))
        except (TypeError, ValueError) as exc:
            raise MarketDataError(f"Unable to construct market data from records: {exc}") from exc
        return self._normalize_and_validate(
            raw,
            symbol=symbol,
            column_mapping=column_mapping,
            row_label="API record",
            row_offset=1,
        )

    def _normalize_and_validate(
        self,
        raw: pd.DataFrame,
        *,
        symbol: str | None,
        column_mapping: Mapping[str, str] | None,
        row_label: str,
        row_offset: int,
    ) -> pd.DataFrame:
        normalized = self._normalize_columns(raw, symbol=symbol, column_mapping=column_mapping)
        normalized.reset_index(drop=True, inplace=True)
        self._parse_timestamps(normalized, row_label=row_label, row_offset=row_offset)
        self._parse_numeric_columns(normalized, row_label=row_label, row_offset=row_offset)
        # String operations preserve missing values so validation can reject them.
        normalized[SYMBOL] = normalized[SYMBOL].str.strip()
        normalized.sort_values([TIMESTAMP, SYMBOL], kind="stable", inplace=True)
        normalized.reset_index(drop=True, inplace=True)
        validate_ohlcv(normalized)
        return normalized

    def _normalize_columns(
        self,
        frame: pd.DataFrame,
        *,
        symbol: str | None,
        column_mapping: Mapping[str, str] | None,
    ) -> pd.DataFrame:
        columns_by_key: dict[str, list[str]] = {}
        for column in frame.columns:
            columns_by_key.setdefault(_column_key(column), []).append(str(column))

        resolved: dict[str, str] = {}
        for source, target in (column_mapping or {}).items():
            canonical = target.casefold().strip()
            if canonical not in CANONICAL_COLUMNS:
                raise MarketDataSchemaError(
                    f"Custom mapping target '{target}' is not a canonical OHLCV column"
                )
            source_column = self._resolve_unique_source(columns_by_key, _column_key(source), source)
            if canonical in resolved:
                raise MarketDataSchemaError(
                    f"Multiple source columns map to canonical column '{canonical}'"
                )
            resolved[canonical] = source_column

        for canonical, aliases in _COLUMN_ALIASES.items():
            if canonical in resolved:
                continue
            for alias in aliases:
                matches = columns_by_key.get(alias, [])
                if len(matches) > 1:
                    joined = ", ".join(repr(match) for match in matches)
                    raise MarketDataSchemaError(
                        f"Ambiguous columns for '{canonical}': {joined}"
                    )
                if matches:
                    resolved[canonical] = matches[0]
                    break

        required_from_file = [column for column in CANONICAL_COLUMNS if column != SYMBOL]
        missing = [column for column in required_from_file if column not in resolved]
        if symbol is None and SYMBOL not in resolved:
            missing.append(SYMBOL)
        if missing:
            raise MarketDataSchemaError(
                "Missing required OHLCV columns: " + ", ".join(missing)
            )

        selected = pd.DataFrame(
            {canonical: frame[source_column] for canonical, source_column in resolved.items()}
        )
        if symbol is not None:
            clean_symbol = symbol.strip()
            if not clean_symbol:
                raise MarketDataSchemaError("The supplied symbol cannot be empty")
            if SYMBOL in selected:
                observed = selected[SYMBOL].dropna().astype(str).str.strip()
                conflicts = observed.ne(clean_symbol)
                if conflicts.any():
                    bad = observed[conflicts].iloc[0]
                    raise MarketDataValidationError(
                        f"Supplied symbol '{clean_symbol}' conflicts with CSV symbol '{bad}'"
                    )
            selected[SYMBOL] = clean_symbol

        return selected.loc[:, list(CANONICAL_COLUMNS)].copy()

    @staticmethod
    def _resolve_unique_source(
        columns_by_key: Mapping[str, list[str]], key: str, requested: str
    ) -> str:
        matches = columns_by_key.get(key, [])
        if not matches:
            raise MarketDataSchemaError(
                f"Custom mapping references missing source column '{requested}'"
            )
        if len(matches) > 1:
            joined = ", ".join(repr(match) for match in matches)
            raise MarketDataSchemaError(
                f"Custom mapping source '{requested}' is ambiguous: {joined}"
            )
        return matches[0]

    @staticmethod
    def _parse_timestamps(
        frame: pd.DataFrame, *, row_label: str, row_offset: int
    ) -> None:
        original = frame[TIMESTAMP]
        parsed = pd.to_datetime(original, errors="coerce", format="mixed")
        invalid = parsed.isna()
        if invalid.any():
            index = invalid.index[invalid.to_numpy().nonzero()[0][0]]
            raise MarketDataValidationError(
                f"Invalid timestamp at {row_label} {int(index) + row_offset}: "
                f"{original.at[index]!r}"
            )
        frame[TIMESTAMP] = parsed

    @staticmethod
    def _parse_numeric_columns(
        frame: pd.DataFrame, *, row_label: str, row_offset: int
    ) -> None:
        for column in NUMERIC_COLUMNS:
            original = frame[column]
            parsed = pd.to_numeric(original, errors="coerce")
            invalid = parsed.isna()
            if invalid.any():
                index = invalid.index[invalid.to_numpy().nonzero()[0][0]]
                raise MarketDataValidationError(
                    f"Invalid numeric value for '{column}' at {row_label} "
                    f"{int(index) + row_offset}: {original.at[index]!r}"
                )
            frame[column] = parsed.astype(float)
