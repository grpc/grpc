from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def randomized_partitioning(G, seed: Incomplete | None = None, p: float = 0.5, weight: Incomplete | None = None): ...
@_dispatch
def one_exchange(G, initial_cut: Incomplete | None = None, seed: Incomplete | None = None, weight: Incomplete | None = None): ...
