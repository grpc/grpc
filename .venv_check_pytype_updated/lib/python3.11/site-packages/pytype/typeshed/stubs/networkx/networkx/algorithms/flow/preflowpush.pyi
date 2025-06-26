from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def preflow_push(
    G,
    s,
    t,
    capacity: str = "capacity",
    residual: Incomplete | None = None,
    global_relabel_freq: float = 1,
    value_only: bool = False,
): ...
