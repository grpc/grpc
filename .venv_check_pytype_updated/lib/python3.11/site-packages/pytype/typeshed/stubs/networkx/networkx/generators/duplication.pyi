from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def partial_duplication_graph(N, n, p, q, seed: Incomplete | None = None): ...
@_dispatch
def duplication_divergence_graph(n, p, seed: Incomplete | None = None): ...
