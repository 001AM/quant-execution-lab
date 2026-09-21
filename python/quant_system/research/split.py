"""Chronological train/test and walk-forward splitting."""

import math
from dataclasses import dataclass

import pandas as pd

from quant_system.data.models import TIMESTAMP


@dataclass(frozen=True, slots=True)
class WalkForwardSplit:
    """One chronological train/test fold with explicit boundaries."""

    fold: int
    train_start: object
    train_end: object
    test_start: object
    test_end: object
    train_data: pd.DataFrame
    test_data: pd.DataFrame


def _validated_data(data: pd.DataFrame) -> pd.DataFrame:
    if not isinstance(data, pd.DataFrame):
        raise TypeError("data must be a pandas DataFrame")
    if data.empty:
        raise ValueError("data must not be empty")
    ordering = data[TIMESTAMP] if TIMESTAMP in data.columns else data.index.to_series()
    if ordering.isna().any():
        raise ValueError("time-series ordering values must not be missing")
    if not ordering.is_monotonic_increasing:
        raise ValueError("data must be sorted chronologically")
    return data


def _positive_size(name: str, value: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise TypeError(f"{name} must be an integer")
    if value <= 0:
        raise ValueError(f"{name} must be positive")
    return value


def _boundary(data: pd.DataFrame, position: int) -> object:
    if TIMESTAMP in data.columns:
        return data.iloc[position][TIMESTAMP]
    return data.index[position]


def time_series_split(
    data: pd.DataFrame,
    train_ratio: float = 0.7,
) -> tuple[pd.DataFrame, pd.DataFrame]:
    """Split earlier observations into train and later observations into test."""

    values = _validated_data(data)
    if isinstance(train_ratio, bool) or not isinstance(train_ratio, int | float):
        raise TypeError("train_ratio must be a real number")
    ratio = float(train_ratio)
    if not math.isfinite(ratio) or not 0.0 < ratio < 1.0:
        raise ValueError("train_ratio must be finite and between zero and one")
    if len(values) < 2:
        raise ValueError("data requires at least two observations")

    split_at = int(len(values) * ratio)
    if split_at == 0 or split_at == len(values):
        raise ValueError("train_ratio must leave at least one row in train and test")
    return values.iloc[:split_at].copy(), values.iloc[split_at:].copy()


def walk_forward_splits(
    data: pd.DataFrame,
    train_size: int,
    test_size: int,
    step_size: int | None = None,
    *,
    expanding: bool = False,
) -> list[WalkForwardSplit]:
    """Build deterministic rolling- or expanding-window chronological folds."""

    values = _validated_data(data)
    train_count = _positive_size("train_size", train_size)
    test_count = _positive_size("test_size", test_size)
    step_count = test_count if step_size is None else _positive_size("step_size", step_size)
    if not isinstance(expanding, bool):
        raise TypeError("expanding must be a bool")
    if train_count + test_count > len(values):
        raise ValueError("data is too short for one complete walk-forward fold")

    splits: list[WalkForwardSplit] = []
    test_start = train_count
    fold = 1
    while test_start + test_count <= len(values):
        train_start = 0 if expanding else test_start - train_count
        train_end = test_start
        test_end = test_start + test_count
        train_data = values.iloc[train_start:train_end].copy()
        test_data = values.iloc[test_start:test_end].copy()
        splits.append(
            WalkForwardSplit(
                fold=fold,
                train_start=_boundary(values, train_start),
                train_end=_boundary(values, train_end - 1),
                test_start=_boundary(values, test_start),
                test_end=_boundary(values, test_end - 1),
                train_data=train_data,
                test_data=test_data,
            )
        )
        fold += 1
        test_start += step_count
    return splits
