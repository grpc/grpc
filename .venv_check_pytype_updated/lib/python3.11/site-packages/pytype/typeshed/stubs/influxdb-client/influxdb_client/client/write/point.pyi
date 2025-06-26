from _typeshed import Incomplete, SupportsGetItem, SupportsItems
from collections.abc import Iterable
from datetime import datetime, timedelta
from numbers import Integral
from typing import Any, Literal
from typing_extensions import Self, TypeAlias

from influxdb_client.domain.write_precision import _WritePrecision

_Value: TypeAlias = Incomplete
_Time: TypeAlias = Integral | str | datetime | timedelta

EPOCH: datetime
DEFAULT_WRITE_PRECISION: _WritePrecision

class Point:
    @staticmethod
    def measurement(measurement: str) -> Point: ...
    @staticmethod
    def from_dict(
        dictionary: SupportsGetItem[str, Any],
        write_precision: _WritePrecision = "ns",
        *,
        record_measurement_name: str | None = ...,
        record_measurement_key: str = ...,
        record_tag_keys: Iterable[str] | None = ...,
        record_field_keys: Iterable[str] | None = ...,
        record_time_key: str = ...,
        fields: SupportsItems[str, Literal["int", "uint", "float"]] = ...,
    ) -> Point: ...
    def __init__(self, measurement_name: str) -> None: ...
    def time(self, time: _Time, write_precision: _WritePrecision = "ns") -> Self: ...
    def tag(self, key: str, value: _Value) -> Self: ...
    def field(self, field: str, value: _Value) -> Self: ...
    def to_line_protocol(self, precision: _WritePrecision | None = None) -> str: ...
    @property
    def write_precision(self) -> _WritePrecision: ...
    @classmethod
    def set_str_rep(cls, rep_function: Any) -> None: ...
    def __eq__(self, other: object) -> bool: ...
