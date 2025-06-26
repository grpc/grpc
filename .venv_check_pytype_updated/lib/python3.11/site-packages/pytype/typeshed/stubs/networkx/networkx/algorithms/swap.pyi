from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def directed_edge_swap(G, *, nswap: int = 1, max_tries: int = 100, seed: Incomplete | None = None): ...
@_dispatch
def double_edge_swap(G, nswap: int = 1, max_tries: int = 100, seed: Incomplete | None = None): ...
@_dispatch
def connected_double_edge_swap(G, nswap: int = 1, _window_threshold: int = 3, seed: Incomplete | None = None): ...
