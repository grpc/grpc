from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def pagerank(
    G,
    alpha: float = 0.85,
    personalization: Incomplete | None = None,
    max_iter: int = 100,
    tol: float = 1e-06,
    nstart: Incomplete | None = None,
    weight: str = "weight",
    dangling: Incomplete | None = None,
): ...
@_dispatch
def google_matrix(
    G,
    alpha: float = 0.85,
    personalization: Incomplete | None = None,
    nodelist: Incomplete | None = None,
    weight: str = "weight",
    dangling: Incomplete | None = None,
): ...
