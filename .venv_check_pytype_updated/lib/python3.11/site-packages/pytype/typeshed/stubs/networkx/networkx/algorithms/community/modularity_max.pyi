from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def greedy_modularity_communities(
    G, weight: Incomplete | None = None, resolution: float = 1, cutoff: int = 1, best_n: Incomplete | None = None
): ...
@_dispatch
def naive_greedy_modularity_communities(G, resolution: float = 1, weight: Incomplete | None = None): ...
