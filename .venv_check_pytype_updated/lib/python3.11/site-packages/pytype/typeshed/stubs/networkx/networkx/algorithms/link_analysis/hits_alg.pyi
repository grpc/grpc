from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def hits(G, max_iter: int = 100, tol: float = 1e-08, nstart: Incomplete | None = None, normalized: bool = True): ...
