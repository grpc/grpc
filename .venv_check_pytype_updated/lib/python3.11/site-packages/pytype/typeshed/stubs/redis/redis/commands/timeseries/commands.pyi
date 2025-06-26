from typing import Literal
from typing_extensions import TypeAlias

_Key: TypeAlias = bytes | str | memoryview

ADD_CMD: Literal["TS.ADD"]
ALTER_CMD: Literal["TS.ALTER"]
CREATERULE_CMD: Literal["TS.CREATERULE"]
CREATE_CMD: Literal["TS.CREATE"]
DECRBY_CMD: Literal["TS.DECRBY"]
DELETERULE_CMD: Literal["TS.DELETERULE"]
DEL_CMD: Literal["TS.DEL"]
GET_CMD: Literal["TS.GET"]
INCRBY_CMD: Literal["TS.INCRBY"]
INFO_CMD: Literal["TS.INFO"]
MADD_CMD: Literal["TS.MADD"]
MGET_CMD: Literal["TS.MGET"]
MRANGE_CMD: Literal["TS.MRANGE"]
MREVRANGE_CMD: Literal["TS.MREVRANGE"]
QUERYINDEX_CMD: Literal["TS.QUERYINDEX"]
RANGE_CMD: Literal["TS.RANGE"]
REVRANGE_CMD: Literal["TS.REVRANGE"]

class TimeSeriesCommands:
    def create(
        self,
        key: _Key,
        retention_msecs: int | None = None,
        uncompressed: bool | None = False,
        labels: dict[str, str] | None = None,
        chunk_size: int | None = None,
        duplicate_policy: str | None = None,
    ): ...
    def alter(
        self,
        key: _Key,
        retention_msecs: int | None = None,
        labels: dict[str, str] | None = None,
        chunk_size: int | None = None,
        duplicate_policy: str | None = None,
    ): ...
    def add(
        self,
        key: _Key,
        timestamp: int | str,
        value: float,
        retention_msecs: int | None = None,
        uncompressed: bool | None = False,
        labels: dict[str, str] | None = None,
        chunk_size: int | None = None,
        duplicate_policy: str | None = None,
    ): ...
    def madd(self, ktv_tuples): ...
    def incrby(
        self,
        key: _Key,
        value: float,
        timestamp: int | str | None = None,
        retention_msecs: int | None = None,
        uncompressed: bool | None = False,
        labels: dict[str, str] | None = None,
        chunk_size: int | None = None,
    ): ...
    def decrby(
        self,
        key: _Key,
        value: float,
        timestamp: int | str | None = None,
        retention_msecs: int | None = None,
        uncompressed: bool | None = False,
        labels: dict[str, str] | None = None,
        chunk_size: int | None = None,
    ): ...
    def delete(self, key, from_time, to_time): ...
    def createrule(
        self, source_key: _Key, dest_key: _Key, aggregation_type: str, bucket_size_msec: int, align_timestamp: int | None = None
    ): ...
    def deleterule(self, source_key, dest_key): ...
    def range(
        self,
        key: _Key,
        from_time: int | str,
        to_time: int | str,
        count: int | None = None,
        aggregation_type: str | None = None,
        bucket_size_msec: int | None = 0,
        filter_by_ts: list[int] | None = None,
        filter_by_min_value: int | None = None,
        filter_by_max_value: int | None = None,
        align: int | str | None = None,
        latest: bool | None = False,
        bucket_timestamp: str | None = None,
        empty: bool | None = False,
    ): ...
    def revrange(
        self,
        key: _Key,
        from_time: int | str,
        to_time: int | str,
        count: int | None = None,
        aggregation_type: str | None = None,
        bucket_size_msec: int | None = 0,
        filter_by_ts: list[int] | None = None,
        filter_by_min_value: int | None = None,
        filter_by_max_value: int | None = None,
        align: int | str | None = None,
        latest: bool | None = False,
        bucket_timestamp: str | None = None,
        empty: bool | None = False,
    ): ...
    def mrange(
        self,
        from_time: int | str,
        to_time: int | str,
        filters: list[str],
        count: int | None = None,
        aggregation_type: str | None = None,
        bucket_size_msec: int | None = 0,
        with_labels: bool | None = False,
        filter_by_ts: list[int] | None = None,
        filter_by_min_value: int | None = None,
        filter_by_max_value: int | None = None,
        groupby: str | None = None,
        reduce: str | None = None,
        select_labels: list[str] | None = None,
        align: int | str | None = None,
        latest: bool | None = False,
        bucket_timestamp: str | None = None,
        empty: bool | None = False,
    ): ...
    def mrevrange(
        self,
        from_time: int | str,
        to_time: int | str,
        filters: list[str],
        count: int | None = None,
        aggregation_type: str | None = None,
        bucket_size_msec: int | None = 0,
        with_labels: bool | None = False,
        filter_by_ts: list[int] | None = None,
        filter_by_min_value: int | None = None,
        filter_by_max_value: int | None = None,
        groupby: str | None = None,
        reduce: str | None = None,
        select_labels: list[str] | None = None,
        align: int | str | None = None,
        latest: bool | None = False,
        bucket_timestamp: str | None = None,
        empty: bool | None = False,
    ): ...
    def get(self, key: _Key, latest: bool | None = False): ...
    def mget(
        self,
        filters: list[str],
        with_labels: bool | None = False,
        select_labels: list[str] | None = None,
        latest: bool | None = False,
    ): ...
    def info(self, key): ...
    def queryindex(self, filters): ...
