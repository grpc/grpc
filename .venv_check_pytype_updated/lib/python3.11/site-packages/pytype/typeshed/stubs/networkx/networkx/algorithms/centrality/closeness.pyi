from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def closeness_centrality(G, u: Incomplete | None = None, distance: Incomplete | None = None, wf_improved: bool = True): ...
@_dispatch
def incremental_closeness_centrality(
    G, edge, prev_cc: Incomplete | None = None, insertion: bool = True, wf_improved: bool = True
): ...
