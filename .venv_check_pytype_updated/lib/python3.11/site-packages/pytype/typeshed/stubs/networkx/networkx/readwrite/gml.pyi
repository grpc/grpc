from _typeshed import Incomplete
from collections.abc import Generator
from enum import Enum
from typing import Generic, NamedTuple, TypeVar

from networkx.utils.backends import _dispatch

_T = TypeVar("_T")

__all__ = ["read_gml", "parse_gml", "generate_gml", "write_gml"]

@_dispatch
def read_gml(path, label: str = "label", destringizer: Incomplete | None = None): ...
@_dispatch
def parse_gml(lines, label: str = "label", destringizer: Incomplete | None = None): ...

class Pattern(Enum):
    KEYS = 0
    REALS = 1
    INTS = 2
    STRINGS = 3
    DICT_START = 4
    DICT_END = 5
    COMMENT_WHITESPACE = 6

class Token(NamedTuple, Generic[_T]):
    category: Pattern
    value: _T
    line: int
    position: int

def generate_gml(G, stringizer: Incomplete | None = None) -> Generator[Incomplete, Incomplete, None]: ...
def write_gml(G, path, stringizer: Incomplete | None = None) -> None: ...
