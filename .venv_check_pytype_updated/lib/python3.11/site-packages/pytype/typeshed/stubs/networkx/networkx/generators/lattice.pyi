from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def grid_2d_graph(m, n, periodic: bool = False, create_using: Incomplete | None = None): ...
@_dispatch
def grid_graph(dim, periodic: bool = False): ...
@_dispatch
def hypercube_graph(n): ...
@_dispatch
def triangular_lattice_graph(
    m, n, periodic: bool = False, with_positions: bool = True, create_using: Incomplete | None = None
): ...
@_dispatch
def hexagonal_lattice_graph(
    m, n, periodic: bool = False, with_positions: bool = True, create_using: Incomplete | None = None
): ...
