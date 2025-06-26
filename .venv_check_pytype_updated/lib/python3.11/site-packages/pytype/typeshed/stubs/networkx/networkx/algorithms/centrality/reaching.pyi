from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def global_reaching_centrality(G, weight: Incomplete | None = None, normalized: bool = True): ...
@_dispatch
def local_reaching_centrality(
    G, v, paths: Incomplete | None = None, weight: Incomplete | None = None, normalized: bool = True
): ...
