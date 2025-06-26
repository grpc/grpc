from _typeshed import Incomplete
from collections.abc import Callable, Iterator
from typing import Any

class _StatsProperty:
    name: str
    func: Callable[..., Any]
    internal_name: str
    __doc__: str | None
    def __init__(self, name: str, func: Callable[..., Any]) -> None: ...
    def __get__(self, obj: object, objtype: Any | None = None) -> Any: ...

class Stats:
    data: list[float]
    default: float
    def __init__(self, data: list[float], default: float = 0.0, use_copy: bool = True, is_sorted: bool = False) -> None: ...
    def __len__(self) -> int: ...
    def __iter__(self) -> Iterator[float]: ...
    def clear_cache(self) -> None: ...
    count: _StatsProperty
    mean: _StatsProperty
    max: _StatsProperty
    min: _StatsProperty
    median: _StatsProperty
    iqr: _StatsProperty
    trimean: _StatsProperty
    variance: _StatsProperty
    std_dev: _StatsProperty
    median_abs_dev: _StatsProperty
    mad: _StatsProperty
    rel_std_dev: _StatsProperty
    skewness: _StatsProperty
    kurtosis: _StatsProperty
    pearson_type: _StatsProperty
    def get_quantile(self, q: float) -> float: ...
    def get_zscore(self, value: float) -> float: ...
    def trim_relative(self, amount: float = 0.15) -> None: ...
    def get_histogram_counts(self, bins: int | None = None, **kw) -> int: ...
    def format_histogram(self, bins: int | None = None, **kw) -> str: ...
    def describe(
        self, quantiles: list[float] | None = None, format: str | None = None
    ) -> dict[str, float] | list[float] | str: ...

def describe(
    data: list[float], quantiles: list[float] | None = None, format: str | None = None
) -> dict[str, float] | list[float] | str: ...

mean: Incomplete
median: Incomplete
iqr: Incomplete
trimean: Incomplete
variance: Incomplete
std_dev: Incomplete
median_abs_dev: Incomplete
rel_std_dev: Incomplete
skewness: Incomplete
kurtosis: Incomplete
pearson_type: Incomplete

def format_histogram_counts(
    bin_counts: list[float], width: int | None = None, format_bin: Callable[..., Any] | None = None
) -> str: ...
