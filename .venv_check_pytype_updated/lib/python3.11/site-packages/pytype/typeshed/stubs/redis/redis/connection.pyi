from _typeshed import Incomplete, Unused
from abc import abstractmethod
from collections.abc import Callable, Iterable, Mapping
from queue import Queue
from socket import socket
from typing import Any, ClassVar
from typing_extensions import Self, TypeAlias

from .credentials import CredentialProvider
from .retry import Retry

ssl_available: bool
SYM_STAR: bytes
SYM_DOLLAR: bytes
SYM_CRLF: bytes
SYM_EMPTY: bytes
SERVER_CLOSED_CONNECTION_ERROR: str
NONBLOCKING_EXCEPTIONS: tuple[type[Exception], ...]
NONBLOCKING_EXCEPTION_ERROR_NUMBERS: dict[type[Exception], int]
SENTINEL: object
MODULE_LOAD_ERROR: str
NO_SUCH_MODULE_ERROR: str
MODULE_UNLOAD_NOT_POSSIBLE_ERROR: str
MODULE_EXPORTS_DATA_TYPES_ERROR: str
FALSE_STRINGS: tuple[str, ...]
URL_QUERY_ARGUMENT_PARSERS: dict[str, Callable[[Any], Any]]

# Options as passed to Pool.get_connection().
_ConnectionPoolOptions: TypeAlias = Any
_ConnectFunc: TypeAlias = Callable[[Connection], object]

class BaseParser:
    EXCEPTION_CLASSES: ClassVar[dict[str, type[Exception] | dict[str, type[Exception]]]]
    @classmethod
    def parse_error(cls, response: str) -> Exception: ...

class SocketBuffer:
    socket_read_size: int
    bytes_written: int
    bytes_read: int
    socket_timeout: float | None
    def __init__(self, socket: socket, socket_read_size: int, socket_timeout: float | None) -> None: ...
    def unread_bytes(self) -> int: ...
    def can_read(self, timeout: float | None) -> bool: ...
    def read(self, length: int) -> bytes: ...
    def readline(self) -> bytes: ...
    def get_pos(self) -> int: ...
    def rewind(self, pos: int) -> None: ...
    def purge(self) -> None: ...
    def close(self) -> None: ...

class PythonParser(BaseParser):
    encoding: str
    socket_read_size: int
    encoder: Encoder | None
    def __init__(self, socket_read_size: int) -> None: ...
    def __del__(self) -> None: ...
    def on_connect(self, connection: Connection) -> None: ...
    def on_disconnect(self) -> None: ...
    def can_read(self, timeout: float | None) -> bool: ...
    def read_response(self, disable_decoding: bool = False) -> Any: ...  # `str | bytes` or `list[str | bytes]`

class HiredisParser(BaseParser):
    socket_read_size: int
    def __init__(self, socket_read_size: int) -> None: ...
    def __del__(self) -> None: ...
    def on_connect(self, connection: Connection, **kwargs) -> None: ...
    def on_disconnect(self) -> None: ...
    def can_read(self, timeout: float | None) -> bool: ...
    def read_from_socket(self, timeout: float | None = ..., raise_on_timeout: bool = True) -> bool: ...
    def read_response(self, disable_decoding: bool = False) -> Any: ...  # `str | bytes` or `list[str | bytes]`

DefaultParser: type[BaseParser]  # Hiredis or PythonParser

_Encodable: TypeAlias = str | bytes | memoryview | bool | float

class Encoder:
    encoding: str
    encoding_errors: str
    decode_responses: bool
    def __init__(self, encoding: str, encoding_errors: str, decode_responses: bool) -> None: ...
    def encode(self, value: _Encodable) -> bytes: ...
    def decode(self, value: str | bytes | memoryview, force: bool = False) -> str: ...

class AbstractConnection:
    pid: int
    db: int
    client_name: str | None
    credential_provider: CredentialProvider | None
    password: str | None
    username: str | None
    socket_timeout: float | None
    socket_connect_timeout: float | None
    retry_on_timeout: bool
    retry_on_error: list[type[Exception]]
    retry: Retry
    health_check_interval: int
    next_health_check: int
    redis_connect_func: _ConnectFunc | None
    encoder: Encoder

    def __init__(
        self,
        db: int = 0,
        password: str | None = None,
        socket_timeout: float | None = None,
        socket_connect_timeout: float | None = None,
        retry_on_timeout: bool = False,
        retry_on_error: list[type[Exception]] = ...,
        encoding: str = "utf-8",
        encoding_errors: str = "strict",
        decode_responses: bool = False,
        parser_class: type[BaseParser] = ...,
        socket_read_size: int = 65536,
        health_check_interval: int = 0,
        client_name: str | None = None,
        username: str | None = None,
        retry: Retry | None = None,
        redis_connect_func: _ConnectFunc | None = None,
        credential_provider: CredentialProvider | None = None,
        command_packer: Incomplete | None = None,
    ) -> None: ...
    @abstractmethod
    def repr_pieces(self) -> list[tuple[str, Any]]: ...
    def register_connect_callback(self, callback: _ConnectFunc) -> None: ...
    def clear_connect_callbacks(self) -> None: ...
    def set_parser(self, parser_class: type[BaseParser]) -> None: ...
    def connect(self) -> None: ...
    def on_connect(self) -> None: ...
    def disconnect(self, *args: Unused) -> None: ...  # 'args' added in redis 4.1.2
    def check_health(self) -> None: ...
    def send_packed_command(self, command: str | Iterable[str], check_health: bool = True) -> None: ...
    def send_command(self, *args, **kwargs) -> None: ...
    def can_read(self, timeout: float | None = 0) -> bool: ...
    def read_response(
        self, disable_decoding: bool = False, *, disconnect_on_error: bool = True
    ) -> Any: ...  # `str | bytes` or `list[str | bytes]`
    def pack_command(self, *args) -> list[bytes]: ...
    def pack_commands(self, commands: Iterable[Iterable[Incomplete]]) -> list[bytes]: ...

class Connection(AbstractConnection):
    host: str
    port: int
    socket_keepalive: bool
    socket_keepalive_options: Mapping[str, int | str]
    socket_type: int
    def __init__(
        self,
        host: str = "localhost",
        port: int = 6379,
        socket_keepalive: bool = False,
        socket_keepalive_options: Mapping[str, int | str] | None = None,
        socket_type: int = 0,
        *,
        db: int = 0,
        password: str | None = None,
        socket_timeout: float | None = None,
        socket_connect_timeout: float | None = None,
        retry_on_timeout: bool = False,
        retry_on_error: list[type[Exception]] = ...,
        encoding: str = "utf-8",
        encoding_errors: str = "strict",
        decode_responses: bool = False,
        parser_class: type[BaseParser] = ...,
        socket_read_size: int = 65536,
        health_check_interval: int = 0,
        client_name: str | None = None,
        username: str | None = None,
        retry: Retry | None = None,
        redis_connect_func: _ConnectFunc | None = None,
        credential_provider: CredentialProvider | None = None,
        command_packer: Incomplete | None = None,
    ) -> None: ...
    def repr_pieces(self) -> list[tuple[str, Any]]: ...

class SSLConnection(Connection):
    keyfile: Any
    certfile: Any
    cert_reqs: Any
    ca_certs: Any
    ca_path: Incomplete | None
    check_hostname: bool
    certificate_password: Incomplete | None
    ssl_validate_ocsp: bool
    ssl_validate_ocsp_stapled: bool  # added in 4.1.1
    ssl_ocsp_context: Incomplete | None  # added in 4.1.1
    ssl_ocsp_expected_cert: Incomplete | None  # added in 4.1.1
    def __init__(
        self,
        ssl_keyfile=None,
        ssl_certfile=None,
        ssl_cert_reqs="required",
        ssl_ca_certs=None,
        ssl_ca_data: Incomplete | None = None,
        ssl_check_hostname: bool = False,
        ssl_ca_path: Incomplete | None = None,
        ssl_password: Incomplete | None = None,
        ssl_validate_ocsp: bool = False,
        ssl_validate_ocsp_stapled: bool = False,  # added in 4.1.1
        ssl_ocsp_context: Incomplete | None = None,  # added in 4.1.1
        ssl_ocsp_expected_cert: Incomplete | None = None,  # added in 4.1.1
        *,
        host: str = "localhost",
        port: int = 6379,
        socket_timeout: float | None = None,
        socket_connect_timeout: float | None = None,
        socket_keepalive: bool = False,
        socket_keepalive_options: Mapping[str, int | str] | None = None,
        socket_type: int = 0,
        db: int = 0,
        password: str | None = None,
        retry_on_timeout: bool = False,
        retry_on_error: list[type[Exception]] = ...,
        encoding: str = "utf-8",
        encoding_errors: str = "strict",
        decode_responses: bool = False,
        parser_class: type[BaseParser] = ...,
        socket_read_size: int = 65536,
        health_check_interval: int = 0,
        client_name: str | None = None,
        username: str | None = None,
        retry: Retry | None = None,
        redis_connect_func: _ConnectFunc | None = None,
        credential_provider: CredentialProvider | None = None,
        command_packer: Incomplete | None = None,
    ) -> None: ...

class UnixDomainSocketConnection(AbstractConnection):
    path: str
    def __init__(
        self,
        path: str = "",
        *,
        db: int = 0,
        password: str | None = None,
        socket_timeout: float | None = None,
        socket_connect_timeout: float | None = None,
        retry_on_timeout: bool = False,
        retry_on_error: list[type[Exception]] = ...,
        encoding: str = "utf-8",
        encoding_errors: str = "strict",
        decode_responses: bool = False,
        parser_class: type[BaseParser] = ...,
        socket_read_size: int = 65536,
        health_check_interval: int = 0,
        client_name: str | None = None,
        username: str | None = None,
        retry: Retry | None = None,
        redis_connect_func: _ConnectFunc | None = None,
        credential_provider: CredentialProvider | None = None,
        command_packer: Incomplete | None = None,
    ) -> None: ...
    def repr_pieces(self) -> list[tuple[str, Any]]: ...

# TODO: make generic on `connection_class`
class ConnectionPool:
    connection_class: type[Connection]
    connection_kwargs: dict[str, Any]
    max_connections: int
    pid: int
    @classmethod
    def from_url(cls, url: str, *, db: int = ..., decode_components: bool = ..., **kwargs) -> Self: ...
    def __init__(
        self, connection_class: type[AbstractConnection] = ..., max_connections: int | None = None, **connection_kwargs
    ) -> None: ...
    def reset(self) -> None: ...
    def get_connection(self, command_name: Unused, *keys, **options: _ConnectionPoolOptions) -> Connection: ...
    def make_connection(self) -> Connection: ...
    def release(self, connection: Connection) -> None: ...
    def disconnect(self, inuse_connections: bool = True) -> None: ...
    def get_encoder(self) -> Encoder: ...
    def owns_connection(self, connection: Connection) -> bool: ...

class BlockingConnectionPool(ConnectionPool):
    queue_class: type[Queue[Any]]
    timeout: float
    pool: Queue[Connection | None]  # might not be defined
    def __init__(
        self,
        max_connections: int = 50,
        timeout: float = 20,
        connection_class: type[Connection] = ...,
        queue_class: type[Queue[Any]] = ...,
        **connection_kwargs,
    ) -> None: ...
    def disconnect(self) -> None: ...  # type: ignore[override]

def to_bool(value: object) -> bool: ...
def parse_url(url: str) -> dict[str, Any]: ...
