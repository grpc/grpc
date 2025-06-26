from collections.abc import Iterable, Sequence
from socket import socket
from typing import Any

from .compat import HAS_IPV6 as HAS_IPV6, PY2 as PY2, WIN as WIN, string_types as string_types
from .proxy_headers import PROXY_HEADERS as PROXY_HEADERS

truthy: frozenset[Any]
KNOWN_PROXY_HEADERS: frozenset[Any]

def asbool(s: bool | str | int | None) -> bool: ...
def asoctal(s: str) -> int: ...
def aslist_cronly(value: str) -> list[str]: ...
def aslist(value: str) -> list[str]: ...
def asset(value: str | None) -> set[str]: ...
def slash_fixed_str(s: str | None) -> str: ...
def str_iftruthy(s: str | None) -> str | None: ...
def as_socket_list(sockets: Sequence[object]) -> list[socket]: ...

class _str_marker(str): ...
class _int_marker(int): ...
class _bool_marker: ...

class Adjustments:
    host: _str_marker
    port: _int_marker
    listen: list[str]
    threads: int
    trusted_proxy: str | None
    trusted_proxy_count: int | None
    trusted_proxy_headers: set[str]
    log_untrusted_proxy_headers: bool
    clear_untrusted_proxy_headers: _bool_marker | bool
    url_scheme: str
    url_prefix: str
    ident: str
    backlog: int
    recv_bytes: int
    send_bytes: int
    outbuf_overflow: int
    outbuf_high_watermark: int
    inbuf_overflow: int
    connection_limit: int
    cleanup_interval: int
    channel_timeout: int
    log_socket_errors: bool
    max_request_header_size: int
    max_request_body_size: int
    expose_tracebacks: bool
    unix_socket: str | None
    unix_socket_perms: int
    socket_options: list[tuple[int, int, int]]
    asyncore_loop_timeout: int
    asyncore_use_poll: bool
    ipv4: bool
    ipv6: bool
    sockets: list[socket]
    def __init__(self, **kw: Any) -> None: ...
    @classmethod
    def parse_args(cls, argv: str) -> tuple[dict[str, Any], Any]: ...
    @classmethod
    def check_sockets(cls, sockets: Iterable[socket]) -> None: ...
