from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

__all__ = ["load_centrality", "edge_load_centrality"]

@_dispatch
def newman_betweenness_centrality(
    G, v: Incomplete | None = None, cutoff: Incomplete | None = None, normalized: bool = True, weight: Incomplete | None = None
): ...

load_centrality = newman_betweenness_centrality

@_dispatch
def edge_load_centrality(G, cutoff: bool = False): ...
