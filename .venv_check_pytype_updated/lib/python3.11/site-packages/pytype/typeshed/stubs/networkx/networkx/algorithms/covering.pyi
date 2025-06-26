from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def min_edge_cover(G, matching_algorithm: Incomplete | None = None): ...
@_dispatch
def is_edge_cover(G, cover): ...
