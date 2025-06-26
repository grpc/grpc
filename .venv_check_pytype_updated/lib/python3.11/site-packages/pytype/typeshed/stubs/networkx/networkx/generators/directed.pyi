from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def gn_graph(n, kernel: Incomplete | None = None, create_using: Incomplete | None = None, seed: Incomplete | None = None): ...
@_dispatch
def gnr_graph(n, p, create_using: Incomplete | None = None, seed: Incomplete | None = None): ...
@_dispatch
def gnc_graph(n, create_using: Incomplete | None = None, seed: Incomplete | None = None): ...
@_dispatch
def scale_free_graph(
    n,
    alpha: float = 0.41,
    beta: float = 0.54,
    gamma: float = 0.05,
    delta_in: float = 0.2,
    delta_out: float = 0,
    create_using: Incomplete | None = None,
    seed: Incomplete | None = None,
    initial_graph: Incomplete | None = None,
): ...
@_dispatch
def random_k_out_graph(n, k, alpha, self_loops: bool = True, seed: Incomplete | None = None): ...
