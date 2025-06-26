from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def random_reference(G, niter: int = 1, connectivity: bool = True, seed: Incomplete | None = None): ...
@_dispatch
def lattice_reference(
    G, niter: int = 5, D: Incomplete | None = None, connectivity: bool = True, seed: Incomplete | None = None
): ...
@_dispatch
def sigma(G, niter: int = 100, nrand: int = 10, seed: Incomplete | None = None): ...
@_dispatch
def omega(G, niter: int = 5, nrand: int = 10, seed: Incomplete | None = None): ...
