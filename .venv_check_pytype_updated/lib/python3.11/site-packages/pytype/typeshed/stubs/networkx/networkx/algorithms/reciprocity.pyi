from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def reciprocity(G, nodes: Incomplete | None = None): ...
@_dispatch
def overall_reciprocity(G): ...
