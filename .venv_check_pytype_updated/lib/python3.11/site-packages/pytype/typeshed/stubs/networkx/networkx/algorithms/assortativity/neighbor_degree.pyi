from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def average_neighbor_degree(
    G, source: str = "out", target: str = "out", nodes: Incomplete | None = None, weight: Incomplete | None = None
): ...
