from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def betweenness_centrality(
    G,
    k: Incomplete | None = None,
    normalized: bool = True,
    weight: Incomplete | None = None,
    endpoints: bool = False,
    seed: Incomplete | None = None,
): ...
@_dispatch
def edge_betweenness_centrality(
    G, k: Incomplete | None = None, normalized: bool = True, weight: Incomplete | None = None, seed: Incomplete | None = None
): ...
