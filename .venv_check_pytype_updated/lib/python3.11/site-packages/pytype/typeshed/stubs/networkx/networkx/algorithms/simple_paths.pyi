from _typeshed import Incomplete
from collections.abc import Generator, Sequence

from networkx.classes.graph import Graph, _Node
from networkx.utils.backends import _dispatch

__all__ = ["all_simple_paths", "is_simple_path", "shortest_simple_paths", "all_simple_edge_paths"]

@_dispatch
def is_simple_path(G: Graph[_Node], nodes: Sequence[_Node]): ...
@_dispatch
def all_simple_paths(
    G: Graph[_Node], source: _Node, target: _Node, cutoff: Incomplete | None = None
) -> Generator[list[_Node], None, None]: ...
@_dispatch
def all_simple_edge_paths(
    G: Graph[_Node], source: _Node, target: _Node, cutoff: Incomplete | None = None
) -> Generator[list[_Node] | list[tuple[_Node, _Node]], None, list[_Node] | None]: ...
@_dispatch
def shortest_simple_paths(
    G: Graph[_Node], source: _Node, target: _Node, weight: Incomplete | None = None
) -> Generator[list[_Node], None, None]: ...

class PathBuffer:
    paths: Incomplete
    sortedpaths: Incomplete
    counter: Incomplete
    def __init__(self) -> None: ...
    def __len__(self): ...
    def push(self, cost, path) -> None: ...
    def pop(self): ...
