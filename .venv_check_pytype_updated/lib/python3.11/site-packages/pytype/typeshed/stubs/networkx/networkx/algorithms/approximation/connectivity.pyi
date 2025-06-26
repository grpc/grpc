from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def local_node_connectivity(G, source, target, cutoff: Incomplete | None = None): ...
@_dispatch
def node_connectivity(G, s: Incomplete | None = None, t: Incomplete | None = None): ...
@_dispatch
def all_pairs_node_connectivity(G, nbunch: Incomplete | None = None, cutoff: Incomplete | None = None): ...
