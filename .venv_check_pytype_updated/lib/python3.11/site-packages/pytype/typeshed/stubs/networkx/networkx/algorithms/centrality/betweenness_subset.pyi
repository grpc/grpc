from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def betweenness_centrality_subset(G, sources, targets, normalized: bool = False, weight: Incomplete | None = None): ...
@_dispatch
def edge_betweenness_centrality_subset(G, sources, targets, normalized: bool = False, weight: Incomplete | None = None): ...
