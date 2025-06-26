from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

__all__ = ["could_be_isomorphic", "fast_could_be_isomorphic", "faster_could_be_isomorphic", "is_isomorphic"]

@_dispatch
def could_be_isomorphic(G1, G2): ...

graph_could_be_isomorphic = could_be_isomorphic

@_dispatch
def fast_could_be_isomorphic(G1, G2): ...

fast_graph_could_be_isomorphic = fast_could_be_isomorphic

@_dispatch
def faster_could_be_isomorphic(G1, G2): ...

faster_graph_could_be_isomorphic = faster_could_be_isomorphic

@_dispatch
def is_isomorphic(G1, G2, node_match: Incomplete | None = None, edge_match: Incomplete | None = None): ...
