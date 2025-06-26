from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def kernighan_lin_bisection(
    G, partition: Incomplete | None = None, max_iter: int = 10, weight: str = "weight", seed: Incomplete | None = None
): ...
