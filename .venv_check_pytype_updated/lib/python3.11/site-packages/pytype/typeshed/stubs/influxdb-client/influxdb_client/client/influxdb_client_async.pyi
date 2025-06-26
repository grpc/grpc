from _typeshed import Incomplete
from types import TracebackType
from typing_extensions import Self

from influxdb_client.client._base import _BaseClient
from influxdb_client.client.delete_api_async import DeleteApiAsync
from influxdb_client.client.query_api import QueryOptions
from influxdb_client.client.query_api_async import QueryApiAsync
from influxdb_client.client.write_api import PointSettings
from influxdb_client.client.write_api_async import WriteApiAsync

logger: Incomplete

class InfluxDBClientAsync(_BaseClient):
    api_client: Incomplete
    def __init__(
        self,
        url: str,
        token: str | None = None,
        org: str | None = None,
        debug: bool | None = None,
        timeout: int = 10000,
        enable_gzip: bool = False,
        *,
        verify_ssl: bool = ...,
        ssl_ca_cert: Incomplete | None = ...,
        cert_file: Incomplete | None = ...,
        cert_key_file: Incomplete | None = ...,
        cert_key_password: Incomplete | None = ...,
        ssl_context: Incomplete | None = ...,
        proxy: Incomplete | None = ...,
        proxy_headers: Incomplete | None = ...,
        connection_pool_maxsize: int = ...,
        username: Incomplete | None = ...,
        password: Incomplete | None = ...,
        auth_basic: bool = ...,
        retries: bool | Incomplete = ...,
        profilers: Incomplete | None = ...,
    ) -> None: ...
    async def __aenter__(self) -> Self: ...
    async def __aexit__(
        self, exc_type: type[BaseException] | None, exc: BaseException | None, tb: TracebackType | None
    ) -> None: ...
    async def close(self) -> None: ...
    @classmethod
    def from_config_file(
        cls, config_file: str = "config.ini", debug: Incomplete | None = None, enable_gzip: bool = False, **kwargs
    ): ...
    @classmethod
    def from_env_properties(cls, debug: Incomplete | None = None, enable_gzip: bool = False, **kwargs): ...
    async def ping(self) -> bool: ...
    async def version(self) -> str: ...
    async def build(self) -> str: ...
    def query_api(self, query_options: QueryOptions = ...) -> QueryApiAsync: ...
    def write_api(self, point_settings: PointSettings = ...) -> WriteApiAsync: ...
    def delete_api(self) -> DeleteApiAsync: ...
