from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def line_graph(G, create_using: Incomplete | None = None): ...
@_dispatch
def inverse_line_graph(G): ...
