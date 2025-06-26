from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def group_betweenness_centrality(G, C, normalized: bool = True, weight: Incomplete | None = None, endpoints: bool = False): ...
@_dispatch
def prominent_group(
    G,
    k,
    weight: Incomplete | None = None,
    C: Incomplete | None = None,
    endpoints: bool = False,
    normalized: bool = True,
    greedy: bool = False,
): ...
@_dispatch
def group_closeness_centrality(G, S, weight: Incomplete | None = None): ...
@_dispatch
def group_degree_centrality(G, S): ...
@_dispatch
def group_in_degree_centrality(G, S): ...
@_dispatch
def group_out_degree_centrality(G, S): ...
