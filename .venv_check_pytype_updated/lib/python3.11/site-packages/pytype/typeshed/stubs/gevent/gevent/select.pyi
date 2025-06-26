import sys
from collections.abc import Iterable
from select import error as error
from typing import Any

def select(
    rlist: Iterable[Any], wlist: Iterable[Any], xlist: Iterable[Any], timeout: float | None = None
) -> tuple[list[Any], list[Any], list[Any]]: ...

if sys.platform != "win32":
    from select import poll as poll

    __all__ = ["error", "poll", "select"]
else:
    __all__ = ["error", "select"]
