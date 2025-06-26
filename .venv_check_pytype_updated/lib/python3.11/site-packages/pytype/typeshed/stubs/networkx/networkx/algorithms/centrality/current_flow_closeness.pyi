from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def current_flow_closeness_centrality(G, weight: Incomplete | None = None, dtype=..., solver: str = "lu"): ...

information_centrality = current_flow_closeness_centrality
