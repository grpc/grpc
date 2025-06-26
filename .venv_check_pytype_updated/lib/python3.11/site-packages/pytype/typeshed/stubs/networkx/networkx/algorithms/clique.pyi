from _typeshed import SupportsGetItem, Unused
from collections.abc import Generator, Iterable, Iterator, Sized
from typing import overload

from networkx.classes.graph import Graph, _Node
from networkx.utils.backends import _dispatch

@_dispatch
def enumerate_all_cliques(G: Graph[_Node]) -> Generator[list[_Node], None, None]: ...
@_dispatch
def find_cliques(G: Graph[_Node], nodes: SupportsGetItem[slice, _Node] | None = None) -> Generator[list[_Node], None, None]: ...
@_dispatch
def find_cliques_recursive(G: Graph[_Node], nodes: SupportsGetItem[slice, _Node] | None = None) -> Iterator[list[_Node]]: ...
@_dispatch
def make_max_clique_graph(G: Graph[_Node], create_using: type[Graph[_Node]] | None = None) -> Graph[_Node]: ...
@_dispatch
def make_clique_bipartite(
    G: Graph[_Node], fpos: Unused = None, create_using: type[Graph[_Node]] | None = None, name: Unused = None
) -> Graph[_Node]: ...
@overload
def node_clique_number(  # type: ignore[misc]  # Incompatible return types
    G: Graph[_Node],
    nodes: Iterable[_Node] | None = None,
    cliques: Iterable[Iterable[_Node]] | None = None,
    separate_nodes: Unused = False,
) -> dict[_Node, int]: ...
@overload
def node_clique_number(
    G: Graph[_Node], nodes: _Node, cliques: Iterable[Sized] | None = None, separate_nodes: Unused = False
) -> int: ...
