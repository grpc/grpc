from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def percolation_centrality(
    G, attribute: str = "percolation", states: Incomplete | None = None, weight: Incomplete | None = None
): ...
