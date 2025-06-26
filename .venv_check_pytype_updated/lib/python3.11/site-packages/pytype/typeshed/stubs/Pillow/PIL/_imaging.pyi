from _typeshed import Incomplete
from collections.abc import Sequence
from typing import Literal, Protocol

DEFAULT_STRATEGY: Literal[0]
FILTERED: Literal[1]
HUFFMAN_ONLY: Literal[2]
RLE: Literal[3]
FIXED: Literal[4]

class _PixelAccessor(Protocol):  # noqa: Y046
    # PIL has two concrete types for accessing an image's pixels by coordinate lookup:
    # PixelAccess (written in C; not runtime-importable) and PyAccess (written in
    # Python + cffi; is runtime-importable). PixelAccess came first. PyAccess was added
    # in later to support PyPy, but otherwise is intended to expose the same interface
    # PixelAccess.
    #
    # This protocol describes that interface.
    # TODO: should the color args and getter return types be _Color?
    def __setitem__(self, xy: tuple[int, int], color: Incomplete, /) -> None: ...
    def __getitem__(self, xy: tuple[int, int], /) -> Incomplete: ...
    def putpixel(self, xy: tuple[int, int], color: Incomplete, /) -> None: ...
    def getpixel(self, xy: tuple[int, int], /) -> Incomplete: ...

class _Path:
    def __getattr__(self, item: str) -> Incomplete: ...

def path(x: Sequence[tuple[float, float]] | Sequence[float], /) -> _Path: ...
def __getattr__(name: str, /) -> Incomplete: ...
