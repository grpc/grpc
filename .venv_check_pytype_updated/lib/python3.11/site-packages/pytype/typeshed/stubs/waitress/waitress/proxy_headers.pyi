from collections.abc import Callable, Mapping, Sequence
from logging import Logger
from typing import Any, NamedTuple

from .utilities import BadRequest as BadRequest

PROXY_HEADERS: frozenset[Any]

class Forwarded(NamedTuple):
    by: Any
    for_: Any
    host: Any
    proto: Any

class MalformedProxyHeader(Exception):
    header: str
    reason: str
    value: str
    def __init__(self, header: str, reason: str, value: str) -> None: ...

def proxy_headers_middleware(
    app: Any,
    trusted_proxy: str | None = None,
    trusted_proxy_count: int = 1,
    trusted_proxy_headers: set[str] | None = None,
    clear_untrusted: bool = True,
    log_untrusted: bool = False,
    logger: Logger = ...,
) -> Callable[..., Any]: ...
def parse_proxy_headers(
    environ: Mapping[str, str], trusted_proxy_count: int, trusted_proxy_headers: set[str], logger: Logger = ...
) -> set[str]: ...
def strip_brackets(addr: str) -> str: ...
def clear_untrusted_headers(
    environ: Mapping[str, str], untrusted_headers: Sequence[str], log_warning: bool = False, logger: Logger = ...
) -> None: ...
