from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def modularity_matrix(G, nodelist: Incomplete | None = None, weight: Incomplete | None = None): ...
@_dispatch
def directed_modularity_matrix(G, nodelist: Incomplete | None = None, weight: Incomplete | None = None): ...
