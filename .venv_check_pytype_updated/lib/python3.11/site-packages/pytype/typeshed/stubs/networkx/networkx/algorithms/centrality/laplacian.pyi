from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def laplacian_centrality(
    G,
    normalized: bool = True,
    nodelist: Incomplete | None = None,
    weight: str = "weight",
    walk_type: Incomplete | None = None,
    alpha: float = 0.95,
): ...
