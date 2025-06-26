from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def effective_size(G, nodes: Incomplete | None = None, weight: Incomplete | None = None): ...
@_dispatch
def constraint(G, nodes: Incomplete | None = None, weight: Incomplete | None = None): ...
@_dispatch
def local_constraint(G, u, v, weight: Incomplete | None = None): ...
