from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def dominating_set(G, start_with: Incomplete | None = None): ...
@_dispatch
def is_dominating_set(G, nbunch): ...
