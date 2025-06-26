from _typeshed import Incomplete
from collections.abc import AsyncGenerator

from influxdb_client.client._base import _BaseQueryApi
from influxdb_client.client.flux_table import FluxRecord, TableList

class QueryApiAsync(_BaseQueryApi):
    def __init__(self, influxdb_client, query_options=...) -> None: ...
    async def query(
        self, query: str, org: Incomplete | None = None, params: dict[Incomplete, Incomplete] | None = None
    ) -> TableList: ...
    async def query_stream(
        self, query: str, org: Incomplete | None = None, params: dict[Incomplete, Incomplete] | None = None
    ) -> AsyncGenerator[FluxRecord, None]: ...
    async def query_data_frame(
        self,
        query: str,
        org: Incomplete | None = None,
        data_frame_index: list[str] | None = None,
        params: dict[Incomplete, Incomplete] | None = None,
    ): ...
    async def query_data_frame_stream(
        self,
        query: str,
        org: Incomplete | None = None,
        data_frame_index: list[str] | None = None,
        params: dict[Incomplete, Incomplete] | None = None,
    ): ...
    async def query_raw(
        self, query: str, org: Incomplete | None = None, dialect=..., params: dict[Incomplete, Incomplete] | None = None
    ): ...
