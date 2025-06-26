from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def approximate_current_flow_betweenness_centrality(
    G,
    normalized: bool = True,
    weight: Incomplete | None = None,
    dtype=...,
    solver: str = "full",
    epsilon: float = 0.5,
    kmax: int = 10000,
    seed: Incomplete | None = None,
): ...
@_dispatch
def current_flow_betweenness_centrality(
    G, normalized: bool = True, weight: Incomplete | None = None, dtype=..., solver: str = "full"
): ...
@_dispatch
def edge_current_flow_betweenness_centrality(
    G, normalized: bool = True, weight: Incomplete | None = None, dtype=..., solver: str = "full"
): ...
