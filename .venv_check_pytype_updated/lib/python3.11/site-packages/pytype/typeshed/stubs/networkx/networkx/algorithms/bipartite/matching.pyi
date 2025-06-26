from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def hopcroft_karp_matching(G, top_nodes: Incomplete | None = None): ...
@_dispatch
def eppstein_matching(G, top_nodes: Incomplete | None = None): ...
@_dispatch
def to_vertex_cover(G, matching, top_nodes: Incomplete | None = None): ...

maximum_matching = hopcroft_karp_matching

@_dispatch
def minimum_weight_full_matching(G, top_nodes: Incomplete | None = None, weight: str = "weight"): ...
