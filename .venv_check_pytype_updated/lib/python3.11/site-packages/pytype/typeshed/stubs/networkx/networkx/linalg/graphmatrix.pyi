from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def incidence_matrix(
    G,
    nodelist: Incomplete | None = None,
    edgelist: Incomplete | None = None,
    oriented: bool = False,
    weight: Incomplete | None = None,
): ...
@_dispatch
def adjacency_matrix(G, nodelist: Incomplete | None = None, dtype: Incomplete | None = None, weight: str = "weight"): ...
