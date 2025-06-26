from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def latapy_clustering(G, nodes: Incomplete | None = None, mode: str = "dot"): ...

clustering = latapy_clustering

@_dispatch
def average_clustering(G, nodes: Incomplete | None = None, mode: str = "dot"): ...
@_dispatch
def robins_alexander_clustering(G): ...
