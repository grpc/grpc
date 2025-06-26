from _typeshed import Incomplete

from influxdb_client import Configuration

LOGGERS_NAMES: Incomplete

class _BaseClient:
    url: str
    token: str | None
    org: str | None
    default_tags: Incomplete | None
    conf: _Configuration
    auth_header_name: Incomplete | None
    auth_header_value: Incomplete | None
    retries: bool | Incomplete
    profilers: Incomplete | None
    def __init__(
        self,
        url: str,
        token: str | None,
        debug: bool | None = None,
        timeout: int = 10000,
        enable_gzip: bool = False,
        org: str | None = None,
        default_tags: dict[Incomplete, Incomplete] | None = None,
        http_client_logger: str | None = None,
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

class _BaseQueryApi:
    default_dialect: Incomplete
    def __init__(self, influxdb_client, query_options: Incomplete | None = None) -> None: ...

class _BaseWriteApi:
    def __init__(self, influxdb_client, point_settings: Incomplete | None = None) -> None: ...

class _BaseDeleteApi:
    def __init__(self, influxdb_client) -> None: ...

class _Configuration(Configuration):
    enable_gzip: bool
    username: Incomplete
    password: Incomplete
    def __init__(self) -> None: ...
    def update_request_header_params(self, path: str, params: dict[Incomplete, Incomplete]): ...
    def update_request_body(self, path: str, body): ...
