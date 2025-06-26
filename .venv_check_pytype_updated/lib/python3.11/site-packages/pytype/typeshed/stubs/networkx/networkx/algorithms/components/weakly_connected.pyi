from collections.abc import Generator, Hashable

from networkx.classes.graph import Graph, _Node
from networkx.utils.backends import _dispatch

@_dispatch
def weakly_connected_components(G: Graph[_Node]) -> Generator[set[_Node], None, None]: ...
@_dispatch
def number_weakly_connected_components(G: Graph[Hashable]) -> int: ...
@_dispatch
def is_weakly_connected(G: Graph[Hashable]) -> bool: ...
