import pandas as pd
import pytest

from quant_system.research import time_series_split, walk_forward_splits


def numbered_data(size: int = 10) -> pd.DataFrame:
    return pd.DataFrame(
        {
            "timestamp": pd.date_range("2026-01-01", periods=size, freq="D"),
            "value": range(1, size + 1),
        }
    )


def test_time_series_split_is_chronological_and_non_overlapping() -> None:
    train, test = time_series_split(numbered_data(5), train_ratio=0.6)

    assert train["value"].tolist() == [1, 2, 3]
    assert test["value"].tolist() == [4, 5]
    assert train["timestamp"].max() < test["timestamp"].min()


def test_time_series_split_rejects_unsorted_data() -> None:
    data = numbered_data(5).iloc[[0, 2, 1, 3, 4]]

    with pytest.raises(ValueError, match="chronologically"):
        time_series_split(data)


def test_rolling_walk_forward_splits_match_expected_windows() -> None:
    splits = walk_forward_splits(numbered_data(), train_size=4, test_size=2, step_size=2)

    assert len(splits) == 3
    assert [split.train_data["value"].tolist() for split in splits] == [
        [1, 2, 3, 4],
        [3, 4, 5, 6],
        [5, 6, 7, 8],
    ]
    assert [split.test_data["value"].tolist() for split in splits] == [
        [5, 6],
        [7, 8],
        [9, 10],
    ]


def test_expanding_walk_forward_preserves_original_train_start() -> None:
    splits = walk_forward_splits(
        numbered_data(),
        train_size=4,
        test_size=2,
        step_size=2,
        expanding=True,
    )

    assert [split.train_data["value"].tolist() for split in splits] == [
        [1, 2, 3, 4],
        [1, 2, 3, 4, 5, 6],
        [1, 2, 3, 4, 5, 6, 7, 8],
    ]


def test_step_size_controls_fold_advance() -> None:
    splits = walk_forward_splits(numbered_data(), train_size=3, test_size=2, step_size=3)

    assert [split.test_data["value"].tolist() for split in splits] == [[4, 5], [7, 8]]


@pytest.mark.parametrize(
    ("train_size", "test_size", "step_size"),
    [(0, 2, 2), (4, 0, 2), (4, 2, 0)],
)
def test_walk_forward_rejects_invalid_sizes(
    train_size: int,
    test_size: int,
    step_size: int,
) -> None:
    with pytest.raises(ValueError):
        walk_forward_splits(
            numbered_data(),
            train_size=train_size,
            test_size=test_size,
            step_size=step_size,
        )
