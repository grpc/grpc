from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def ego_graph(G, n, radius: float = 1, center: bool = True, undirected: bool = False, distance: Incomplete | None = None): ...
