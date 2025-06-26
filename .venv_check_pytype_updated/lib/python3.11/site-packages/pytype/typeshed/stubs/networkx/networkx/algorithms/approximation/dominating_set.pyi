from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def min_weighted_dominating_set(G, weight: Incomplete | None = None): ...
@_dispatch
def min_edge_dominating_set(G): ...
