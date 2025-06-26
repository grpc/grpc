from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def uniform_random_intersection_graph(n, m, p, seed: Incomplete | None = None): ...
@_dispatch
def k_random_intersection_graph(n, m, k, seed: Incomplete | None = None): ...
@_dispatch
def general_random_intersection_graph(n, m, p, seed: Incomplete | None = None): ...
