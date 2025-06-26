from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def katz_centrality(
    G,
    alpha: float = 0.1,
    beta: float = 1.0,
    max_iter: int = 1000,
    tol: float = 1e-06,
    nstart: Incomplete | None = None,
    normalized: bool = True,
    weight: Incomplete | None = None,
): ...
@_dispatch
def katz_centrality_numpy(
    G, alpha: float = 0.1, beta: float = 1.0, normalized: bool = True, weight: Incomplete | None = None
): ...
