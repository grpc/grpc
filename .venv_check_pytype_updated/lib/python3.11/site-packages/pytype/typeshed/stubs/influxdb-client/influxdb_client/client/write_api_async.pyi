from _typeshed import Incomplete
from collections.abc import Iterable
from typing import Any
from typing_extensions import TypeAlias

from influxdb_client.client._base import _BaseWriteApi
from influxdb_client.client.write.point import Point
from influxdb_client.client.write_api import PointSettings
from influxdb_client.domain.write_precision import _WritePrecision

_DataClass: TypeAlias = Any  # any dataclass
_NamedTuple: TypeAlias = tuple[Any, ...]  # any NamedTuple

logger: Incomplete

class WriteApiAsync(_BaseWriteApi):
    def __init__(self, influxdb_client, point_settings: PointSettings = ...) -> None: ...
    async def write(
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
            | _NamedTuple
            | Iterable[_NamedTuple]
            | _DataClass
            | Iterable[_DataClass]
        ) = None,
        write_precision: _WritePrecision = "ns",
        **kwargs,
    ) -> bool: ...
