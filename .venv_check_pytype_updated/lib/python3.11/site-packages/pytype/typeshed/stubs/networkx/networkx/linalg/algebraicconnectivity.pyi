from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

class _PCGSolver:
    def __init__(self, A, M) -> None: ...
    def solve(self, B, tol): ...

class _LUSolver:
    def __init__(self, A) -> None: ...
    def solve(self, B, tol: Incomplete | None = None): ...

@_dispatch
def algebraic_connectivity(
    G,
    weight: str = "weight",
    normalized: bool = False,
    tol: float = 1e-08,
    method: str = "tracemin_pcg",
    seed: Incomplete | None = None,
): ...
@_dispatch
def fiedler_vector(
    G,
    weight: str = "weight",
    normalized: bool = False,
    tol: float = 1e-08,
    method: str = "tracemin_pcg",
    seed: Incomplete | None = None,
): ...
@_dispatch
def spectral_ordering(
    G,
    weight: str = "weight",
    normalized: bool = False,
    tol: float = 1e-08,
    method: str = "tracemin_pcg",
    seed: Incomplete | None = None,
): ...
@_dispatch
def spectral_bisection(
    G,
    weight: str = "weight",
    normalized: bool = False,
    tol: float = 1e-08,
    method: str = "tracemin_pcg",
    seed: Incomplete | None = None,
): ...
