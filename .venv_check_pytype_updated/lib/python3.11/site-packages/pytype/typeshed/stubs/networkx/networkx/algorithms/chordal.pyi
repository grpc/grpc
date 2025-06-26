import sys
from collections.abc import Generator, Hashable

from networkx.classes.graph import Graph, _Node
from networkx.exception import NetworkXException
from networkx.utils.backends import _dispatch

class NetworkXTreewidthBoundExceeded(NetworkXException): ...

@_dispatch
def is_chordal(G: Graph[Hashable]) -> bool: ...
@_dispatch
def find_induced_nodes(G: Graph[_Node], s: _Node, t: _Node, treewidth_bound: float = sys.maxsize) -> set[_Node]: ...
@_dispatch
def chordal_graph_cliques(G: Graph[_Node]) -> Generator[frozenset[_Node], None, None]: ...
@_dispatch
def chordal_graph_treewidth(G: Graph[Hashable]) -> int: ...
