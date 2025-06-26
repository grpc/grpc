from _typeshed import Incomplete
from collections.abc import Generator

from networkx.utils.backends import _dispatch

def generate_adjlist(G, delimiter: str = " ") -> Generator[Incomplete, None, None]: ...
def write_adjlist(G, path, comments: str = "#", delimiter: str = " ", encoding: str = "utf-8") -> None: ...
@_dispatch
def parse_adjlist(
    lines,
    comments: str = "#",
    delimiter: Incomplete | None = None,
    create_using: Incomplete | None = None,
    nodetype: Incomplete | None = None,
): ...
@_dispatch
def read_adjlist(
    path,
    comments: str = "#",
    delimiter: Incomplete | None = None,
    create_using: Incomplete | None = None,
    nodetype: Incomplete | None = None,
    encoding: str = "utf-8",
): ...
