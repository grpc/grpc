from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def geometric_edges(G, radius, p: float = 2): ...
@_dispatch
def random_geometric_graph(
    n, radius, dim: int = 2, pos: Incomplete | None = None, p: float = 2, seed: Incomplete | None = None
): ...
@_dispatch
def soft_random_geometric_graph(
    n,
    radius,
    dim: int = 2,
    pos: Incomplete | None = None,
    p: float = 2,
    p_dist: Incomplete | None = None,
    seed: Incomplete | None = None,
): ...
@_dispatch
def geographical_threshold_graph(
    n,
    theta,
    dim: int = 2,
    pos: Incomplete | None = None,
    weight: Incomplete | None = None,
    metric: Incomplete | None = None,
    p_dist: Incomplete | None = None,
    seed: Incomplete | None = None,
): ...
@_dispatch
def waxman_graph(
    n,
    beta: float = 0.4,
    alpha: float = 0.1,
    L: Incomplete | None = None,
    domain=(0, 0, 1, 1),
    metric: Incomplete | None = None,
    seed: Incomplete | None = None,
): ...

# docstring marks p as int, but it still works with floats. So I think it's better for consistency
@_dispatch
def navigable_small_world_graph(n, p: float = 1, q: int = 1, r: float = 2, dim: int = 2, seed: Incomplete | None = None): ...
@_dispatch
def thresholded_random_geometric_graph(
    n,
    radius,
    theta,
    dim: int = 2,
    pos: Incomplete | None = None,
    weight: Incomplete | None = None,
    p: float = 2,
    seed: Incomplete | None = None,
): ...
