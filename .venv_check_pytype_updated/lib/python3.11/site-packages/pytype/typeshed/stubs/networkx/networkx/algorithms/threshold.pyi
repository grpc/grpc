from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def is_threshold_graph(G): ...
@_dispatch
def find_threshold_graph(G, create_using: Incomplete | None = None): ...
