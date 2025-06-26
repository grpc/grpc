from _typeshed import Incomplete
from collections.abc import Generator, Iterable
from typing import Literal, TypeVar, overload

from networkx.classes.graph import Graph, _Node
from networkx.utils.backends import _dispatch

_U = TypeVar("_U")

@overload
def edge_boundary(
    G: Graph[_Node],
    nbunch1: Iterable[_Node],
    nbunch2: Iterable[_Node] | None = None,
    data: Literal[False] = False,
    keys: Literal[False] = False,
    default=None,
) -> Generator[tuple[_Node, _Node], None, None]: ...
@overload
def edge_boundary(
    G: Graph[_Node],
    nbunch1: Iterable[_Node],
    nbunch2: Iterable[_Node] | None,
    data: Literal[True],
    keys: Literal[False] = False,
    default=None,
) -> Generator[tuple[_Node, _Node, dict[str, Incomplete]], None, None]: ...
@overload
def edge_boundary(
    G: Graph[_Node],
    nbunch1: Iterable[_Node],
    nbunch2: Iterable[_Node] | None = None,
    *,
    data: Literal[True],
    keys: Literal[False] = False,
    default=None,
) -> Generator[tuple[_Node, _Node, dict[str, Incomplete]], None, None]: ...
@overload
def edge_boundary(
    G: Graph[_Node],
    nbunch1: Iterable[_Node],
    nbunch2: Iterable[_Node] | None,
    data: str,
    keys: Literal[False] = False,
    default: _U | None = None,
) -> Generator[tuple[_Node, _Node, dict[str, _U]], None, None]: ...
@overload
def edge_boundary(
    G: Graph[_Node],
    nbunch1: Iterable[_Node],
    nbunch2: Iterable[_Node] | None = None,
    *,
    data: str,
    keys: Literal[False] = False,
    default: _U | None = None,
) -> Generator[tuple[_Node, _Node, dict[str, _U]], None, None]: ...
@overload
def edge_boundary(
    G: Graph[_Node],
    nbunch1: Iterable[_Node],
    nbunch2: Iterable[_Node] | None,
    data: Literal[False],
    keys: Literal[True],
    default=None,
) -> Generator[tuple[_Node, _Node, int], None, None]: ...
@overload
def edge_boundary(
    G: Graph[_Node],
    nbunch1: Iterable[_Node],
    nbunch2: Iterable[_Node] | None = None,
    data: Literal[False] = False,
    *,
    keys: Literal[True],
    default=None,
) -> Generator[tuple[_Node, _Node, int], None, None]: ...
@overload
def edge_boundary(
    G: Graph[_Node],
    nbunch1: Iterable[_Node],
    nbunch2: Iterable[_Node] | None,
    data: Literal[True],
    keys: Literal[True],
    default=None,
) -> Generator[tuple[_Node, _Node, int, dict[str, Incomplete]], None, None]: ...
@overload
def edge_boundary(
    G: Graph[_Node],
    nbunch1: Iterable[_Node],
    nbunch2: Iterable[_Node] | None = None,
    *,
    data: Literal[True],
    keys: Literal[True],
    default=None,
) -> Generator[tuple[_Node, _Node, int, dict[str, Incomplete]], None, None]: ...
@overload
def edge_boundary(
    G: Graph[_Node],
    nbunch1: Iterable[_Node],
    nbunch2: Iterable[_Node] | None,
    data: str,
    keys: Literal[True],
    default: _U | None = None,
) -> Generator[tuple[_Node, _Node, int, dict[str, _U]], None, None]: ...
@overload
def edge_boundary(
    G: Graph[_Node],
    nbunch1: Iterable[_Node],
    nbunch2: Iterable[_Node] | None = None,
    *,
    data: str,
    keys: Literal[True],
    default: _U | None = None,
) -> Generator[tuple[_Node, _Node, int, dict[str, _U]], None, None]: ...
@_dispatch
def node_boundary(G: Graph[_Node], nbunch1: Iterable[_Node], nbunch2: Iterable[_Node] | None = None) -> set[_Node]: ...
