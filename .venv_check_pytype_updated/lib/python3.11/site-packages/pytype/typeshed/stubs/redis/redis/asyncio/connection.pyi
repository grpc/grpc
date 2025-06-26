import asyncio
import enum
import ssl
from _typeshed import Incomplete
from collections.abc import Callable, Iterable, Mapping
from typing import Any, Literal, Protocol, TypedDict, overload
from typing_extensions import TypeAlias

from redis import RedisError
from redis.asyncio.retry import Retry
from redis.credentials import CredentialProvider
from redis.exceptions import ResponseError
from redis.typing import EncodableT, EncodedT

hiredis: Any
SYM_STAR: bytes
SYM_DOLLAR: bytes
SYM_CRLF: bytes
SYM_LF: bytes
SYM_EMPTY: bytes
SERVER_CLOSED_CONNECTION_ERROR: str

class _Sentinel(enum.Enum):
    sentinel: Any

SENTINEL: Any
MODULE_LOAD_ERROR: str
NO_SUCH_MODULE_ERROR: str
MODULE_UNLOAD_NOT_POSSIBLE_ERROR: str
MODULE_EXPORTS_DATA_TYPES_ERROR: str

class Encoder:
    encoding: Any
    encoding_errors: Any
    decode_responses: Any
    def __init__(self, encoding: str, encoding_errors: str, decode_responses: bool) -> None: ...
    def encode(self, value: EncodableT) -> EncodedT: ...
    def decode(self, value: EncodableT, force: bool = False) -> EncodableT: ...

ExceptionMappingT: TypeAlias = Mapping[str, type[Exception] | Mapping[str, type[Exception]]]

class BaseParser:
    EXCEPTION_CLASSES: ExceptionMappingT
    def __init__(self, socket_read_size: int) -> None: ...
    @classmethod
    def parse_error(cls, response: str) -> ResponseError: ...
    def on_disconnect(self) -> None: ...
    def on_connect(self, connection: Connection): ...
    async def read_response(self, disable_decoding: bool = False) -> EncodableT | ResponseError | list[EncodableT] | None: ...

class PythonParser(BaseParser):
    encoder: Any
    def __init__(self, socket_read_size: int) -> None: ...
    def on_connect(self, connection: Connection): ...
    def on_disconnect(self) -> None: ...
    async def read_response(self, disable_decoding: bool = False) -> EncodableT | ResponseError | None: ...

class HiredisParser(BaseParser):
    def __init__(self, socket_read_size: int) -> None: ...
    def on_connect(self, connection: Connection): ...
    def on_disconnect(self) -> None: ...
    async def read_from_socket(self) -> Literal[True]: ...
    async def read_response(self, disable_decoding: bool = False) -> EncodableT | list[EncodableT]: ...

DefaultParser: type[PythonParser | HiredisParser]

class ConnectCallbackProtocol(Protocol):
    def __call__(self, connection: Connection): ...

class AsyncConnectCallbackProtocol(Protocol):
    async def __call__(self, connection: Connection): ...

ConnectCallbackT: TypeAlias = ConnectCallbackProtocol | AsyncConnectCallbackProtocol

class Connection:
    pid: Any
    host: Any
    port: Any
    db: Any
    username: Any
    client_name: Any
    password: Any
    socket_timeout: float | None
    socket_connect_timeout: float | None
    socket_keepalive: Any
    socket_keepalive_options: Any
    socket_type: Any
    retry_on_timeout: Any
    retry_on_error: list[type[RedisError]]
    retry: Retry
    health_check_interval: Any
    next_health_check: int
    ssl_context: Any
    encoder: Any
    redis_connect_func: ConnectCallbackT | None
    def __init__(
        self,
        *,
        host: str = "localhost",
        port: str | int = 6379,
        db: str | int = 0,
        password: str | None = None,
        socket_timeout: float | None = None,
        socket_connect_timeout: float | None = None,
        socket_keepalive: bool = False,
        socket_keepalive_options: Mapping[int, int | bytes] | None = None,
        socket_type: int = 0,
        retry_on_timeout: bool = False,
        retry_on_error: list[type[RedisError]] | _Sentinel = ...,
        encoding: str = "utf-8",
        encoding_errors: str = "strict",
        decode_responses: bool = False,
        parser_class: type[BaseParser] = ...,
        socket_read_size: int = 65536,
        health_check_interval: float = 0,
        client_name: str | None = None,
        username: str | None = None,
        retry: Retry | None = None,
        redis_connect_func: ConnectCallbackT | None = None,
        encoder_class: type[Encoder] = ...,
        credential_provider: CredentialProvider | None = None,
    ) -> None: ...
    def repr_pieces(self): ...
    @property
    def is_connected(self): ...
    def register_connect_callback(self, callback) -> None: ...
    def clear_connect_callbacks(self) -> None: ...
    def set_parser(self, parser_class) -> None: ...
    async def connect(self) -> None: ...
    async def on_connect(self) -> None: ...
    async def disconnect(self, nowait: bool = False) -> None: ...
    async def check_health(self) -> None: ...
    async def send_packed_command(self, command: bytes | str | Iterable[bytes], check_health: bool = True): ...
    async def send_command(self, *args, **kwargs) -> None: ...
    @overload
    async def read_response(self, *, timeout: float, disconnect_on_error: bool = True) -> Incomplete | None: ...
    @overload
    async def read_response(
        self, disable_decoding: bool, timeout: float, *, disconnect_on_error: bool = True
    ) -> Incomplete | None: ...
    @overload
    async def read_response(self, disable_decoding: bool = False, timeout: None = None, *, disconnect_on_error: bool = True): ...
    def pack_command(self, *args: EncodableT) -> list[bytes]: ...
    def pack_commands(self, commands: Iterable[Iterable[EncodableT]]) -> list[bytes]: ...

class SSLConnection(Connection):
    ssl_context: Any
    def __init__(
        self,
        ssl_keyfile: str | None = None,
        ssl_certfile: str | None = None,
        ssl_cert_reqs: str = "required",
        ssl_ca_certs: str | None = None,
        ssl_ca_data: str | None = None,
        ssl_check_hostname: bool = False,
        **kwargs,
    ) -> None: ...
    @property
    def keyfile(self): ...
    @property
    def certfile(self): ...
    @property
    def cert_reqs(self): ...
    @property
    def ca_certs(self): ...
    @property
    def ca_data(self): ...
    @property
    def check_hostname(self): ...

class RedisSSLContext:
    keyfile: Any
    certfile: Any
    cert_reqs: Any
    ca_certs: Any
    ca_data: Any
    check_hostname: Any
    context: Any
    def __init__(
        self,
        keyfile: str | None = None,
        certfile: str | None = None,
        cert_reqs: str | None = None,
        ca_certs: str | None = None,
        ca_data: str | None = None,
        check_hostname: bool = False,
    ) -> None: ...
    def get(self) -> ssl.SSLContext: ...

class UnixDomainSocketConnection(Connection):
    pid: Any
    path: Any
    db: Any
    username: Any
    client_name: Any
    password: Any
    retry_on_timeout: Any
    retry_on_error: list[type[RedisError]]
    retry: Any
    health_check_interval: Any
    next_health_check: int
    redis_connect_func: ConnectCallbackT | None
    encoder: Any
    def __init__(
        self,
        *,
        path: str = "",
        db: str | int = 0,
        username: str | None = None,
        password: str | None = None,
        socket_timeout: float | None = None,
        socket_connect_timeout: float | None = None,
        encoding: str = "utf-8",
        encoding_errors: str = "strict",
        decode_responses: bool = False,
        retry_on_timeout: bool = False,
        retry_on_error: list[type[RedisError]] | _Sentinel = ...,
        parser_class: type[BaseParser] = ...,
        socket_read_size: int = 65536,
        health_check_interval: float = 0.0,
        client_name: str | None = None,
        retry: Retry | None = None,
        redis_connect_func: ConnectCallbackT | None = None,
        credential_provider: CredentialProvider | None = None,
    ) -> None: ...
    def repr_pieces(self) -> Iterable[tuple[str, str | int]]: ...

FALSE_STRINGS: Any

def to_bool(value) -> bool | None: ...

URL_QUERY_ARGUMENT_PARSERS: Mapping[str, Callable[..., object]]

class ConnectKwargs(TypedDict):
    username: str
    password: str
    connection_class: type[Connection]
    host: str
    port: int
    db: int
    path: str

def parse_url(url: str) -> ConnectKwargs: ...

class ConnectionPool:
    @classmethod
    def from_url(cls, url: str, **kwargs) -> ConnectionPool: ...
    connection_class: Any
    connection_kwargs: Any
    max_connections: Any
    encoder_class: Any
    def __init__(
        self, connection_class: type[Connection] = ..., max_connections: int | None = None, **connection_kwargs
    ) -> None: ...
    pid: Any
    def reset(self) -> None: ...
    async def get_connection(self, command_name, *keys, **options): ...
    def get_encoder(self): ...
    def make_connection(self): ...
    async def release(self, connection: Connection): ...
    def owns_connection(self, connection: Connection): ...
    async def disconnect(self, inuse_connections: bool = True): ...

class BlockingConnectionPool(ConnectionPool):
    queue_class: Any
    timeout: Any
    def __init__(
        self,
        max_connections: int = 50,
        timeout: int | None = 20,
        connection_class: type[Connection] = ...,
        queue_class: type[asyncio.Queue[Any]] = ...,
        **connection_kwargs,
    ) -> None: ...
    pool: Any
    pid: Any
    def reset(self) -> None: ...
    def make_connection(self): ...
    async def get_connection(self, command_name, *keys, **options): ...
    async def release(self, connection: Connection): ...
    async def disconnect(self, inuse_connections: bool = True): ...
