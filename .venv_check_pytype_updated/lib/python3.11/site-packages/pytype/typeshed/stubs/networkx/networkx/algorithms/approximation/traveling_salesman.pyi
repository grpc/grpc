from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def christofides(G, weight: str = "weight", tree: Incomplete | None = None): ...
@_dispatch
def traveling_salesman_problem(
    G, weight: str = "weight", nodes: Incomplete | None = None, cycle: bool = True, method: Incomplete | None = None
): ...
@_dispatch
def asadpour_atsp(G, weight: str = "weight", seed: Incomplete | None = None, source: Incomplete | None = None): ...
@_dispatch
def greedy_tsp(G, weight: str = "weight", source: Incomplete | None = None): ...
@_dispatch
def simulated_annealing_tsp(
    G,
    init_cycle,
    weight: str = "weight",
    source: Incomplete | None = None,
    # docstring says int, but it can be a float and does become a float mid-equation if alpha is also a float
    temp: float = 100,
    move: str = "1-1",
    max_iterations: int = 10,
    N_inner: int = 100,
    alpha: float = 0.01,
    seed: Incomplete | None = None,
): ...
@_dispatch
def threshold_accepting_tsp(
    G,
    init_cycle,
    weight: str = "weight",
    source: Incomplete | None = None,
    threshold: float = 1,
    move: str = "1-1",
    max_iterations: int = 10,
    N_inner: int = 100,
    alpha: float = 0.1,
    seed: Incomplete | None = None,
): ...
