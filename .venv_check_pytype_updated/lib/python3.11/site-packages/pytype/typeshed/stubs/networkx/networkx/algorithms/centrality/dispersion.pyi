from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def dispersion(
    G,
    u: Incomplete | None = None,
    v: Incomplete | None = None,
    normalized: bool = True,
    alpha: float = 1.0,
    b: float = 0.0,
    c: float = 0.0,
): ...
