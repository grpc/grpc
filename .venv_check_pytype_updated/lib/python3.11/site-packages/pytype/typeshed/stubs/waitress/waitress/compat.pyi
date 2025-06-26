from io import TextIOWrapper
from typing import Any, Literal

PY2: Literal[False]
PY3: Literal[True]
WIN: bool
string_types: tuple[str]
integer_types: tuple[int]
class_types: tuple[type]

def unquote_bytes_to_wsgi(bytestring: bytes) -> str: ...
def text_(s: str, encoding: str = ..., errors: str = ...) -> str: ...
def tostr(s: str) -> str: ...
def tobytes(s: str) -> bytes: ...

exec_: Any

def reraise(tp: Any, value: BaseException, tb: str | None = ...) -> None: ...

MAXINT: int
HAS_IPV6: bool
IPPROTO_IPV6: int
IPV6_V6ONLY: int

def set_nonblocking(fd: TextIOWrapper) -> None: ...

ResourceWarning: Warning

def qualname(cls: Any) -> str: ...
