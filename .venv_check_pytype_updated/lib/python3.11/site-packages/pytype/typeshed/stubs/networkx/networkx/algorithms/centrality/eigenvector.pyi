from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def eigenvector_centrality(
    G, max_iter: int = 100, tol: float = 1e-06, nstart: Incomplete | None = None, weight: Incomplete | None = None
): ...
@_dispatch
def eigenvector_centrality_numpy(G, weight: Incomplete | None = None, max_iter: int = 50, tol: float = 0): ...
