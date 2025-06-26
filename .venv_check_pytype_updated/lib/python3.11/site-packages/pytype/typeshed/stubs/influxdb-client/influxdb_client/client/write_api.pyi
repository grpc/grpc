import logging
from _typeshed import Incomplete
from collections.abc import Iterable
from enum import Enum
from types import TracebackType
from typing import Any
from typing_extensions import Self, TypeAlias

from influxdb_client.client._base import _BaseWriteApi
from influxdb_client.client.write.point import Point
from influxdb_client.domain.write_precision import _WritePrecision

_DataClass: TypeAlias = Any  # any dataclass
_NamedTuple: TypeAlias = tuple[Any, ...]  # any NamedTuple
_Observable: TypeAlias = Any  # reactivex.Observable

logger: logging.Logger

class WriteType(Enum):
    batching: int
    asynchronous: int
    synchronous: int

class WriteOptions:
    write_type: WriteType
    batch_size: int
    flush_interval: int
    jitter_interval: int
    retry_interval: int
    max_retries: int
    max_retry_delay: int
    max_retry_time: int
    exponential_base: int
    write_scheduler: Incomplete
    max_close_wait: int
    def __init__(
        self,
        write_type: WriteType = ...,
        batch_size: int = 1_000,
        flush_interval: int = 1_000,
        jitter_interval: int = 0,
        retry_interval: int = 5_000,
        max_retries: int = 5,
        max_retry_delay: int = 125_000,
        max_retry_time: int = 180_000,
        exponential_base: int = 2,
        max_close_wait: int = 300_000,
        write_scheduler=...,
    ) -> None: ...
    def to_retry_strategy(self, **kwargs): ...

SYNCHRONOUS: Incomplete
ASYNCHRONOUS: Incomplete

class PointSettings:
    defaultTags: Incomplete
    def __init__(self, **default_tags) -> None: ...
    def add_default_tag(self, key, value) -> None: ...

class _BatchItemKey:
    bucket: Incomplete
    org: Incomplete
    precision: Incomplete
    def __init__(self, bucket, org, precision="ns") -> None: ...
    def __hash__(self) -> int: ...
    def __eq__(self, o: object) -> bool: ...

class _BatchItem:
    key: Incomplete
    data: Incomplete
    size: Incomplete
    def __init__(self, key: _BatchItemKey, data, size: int = 1) -> None: ...
    def to_key_tuple(self) -> tuple[str, str, str]: ...

class _BatchResponse:
    data: Incomplete
    exception: Incomplete
    def __init__(self, data: _BatchItem, exception: Exception | None = None) -> None: ...

class WriteApi(_BaseWriteApi):
    def __init__(
        self, influxdb_client, write_options: WriteOptions = ..., point_settings: PointSettings = ..., **kwargs
    ) -> None: ...
    def write(
        self,
        bucket: str,
        org: str | None = None,
        record: (
            str
            | Iterable[str]
            | Point
            | Iterable[Point]
            | dict[Incomplete, Incomplete]
            | Iterable[dict[Incomplete, Incomplete]]
            | bytes
            | Iterable[bytes]
            | _Observable
            | _NamedTuple
            | Iterable[_NamedTuple]
            | _DataClass
            | Iterable[_DataClass]
        ) = None,
        write_precision: _WritePrecision = "ns",
        **kwargs,
    ) -> Any: ...
    def flush(self) -> None: ...
    def close(self) -> None: ...
    def __enter__(self) -> Self: ...
    def __exit__(
        self, exc_type: type[BaseException] | None, exc_val: BaseException | None, exc_tb: TracebackType | None
    ) -> None: ...
    def __del__(self) -> None: ...
