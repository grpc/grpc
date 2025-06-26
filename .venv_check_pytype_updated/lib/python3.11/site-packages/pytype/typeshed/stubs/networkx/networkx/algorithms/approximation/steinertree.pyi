from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def metric_closure(G, weight: str = "weight"): ...
@_dispatch
def steiner_tree(G, terminal_nodes, weight: str = "weight", method: Incomplete | None = None): ...
