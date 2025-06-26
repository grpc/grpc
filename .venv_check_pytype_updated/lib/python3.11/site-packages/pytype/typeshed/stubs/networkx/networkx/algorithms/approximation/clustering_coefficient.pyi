from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def average_clustering(G, trials: int = 1000, seed: Incomplete | None = None): ...
