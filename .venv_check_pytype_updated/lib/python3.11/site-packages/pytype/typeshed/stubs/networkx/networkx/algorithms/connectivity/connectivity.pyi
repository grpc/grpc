from _typeshed import Incomplete

from networkx.algorithms.flow import edmonds_karp
from networkx.utils.backends import _dispatch

__all__ = [
    "average_node_connectivity",
    "local_node_connectivity",
    "node_connectivity",
    "local_edge_connectivity",
    "edge_connectivity",
    "all_pairs_node_connectivity",
]

default_flow_func = edmonds_karp

@_dispatch
def local_node_connectivity(
    G,
    s,
    t,
    flow_func: Incomplete | None = None,
    auxiliary: Incomplete | None = None,
    residual: Incomplete | None = None,
    cutoff: Incomplete | None = None,
): ...
@_dispatch
def node_connectivity(G, s: Incomplete | None = None, t: Incomplete | None = None, flow_func: Incomplete | None = None): ...
@_dispatch
def average_node_connectivity(G, flow_func: Incomplete | None = None): ...
@_dispatch
def all_pairs_node_connectivity(G, nbunch: Incomplete | None = None, flow_func: Incomplete | None = None): ...
@_dispatch
def local_edge_connectivity(
    G,
    s,
    t,
    flow_func: Incomplete | None = None,
    auxiliary: Incomplete | None = None,
    residual: Incomplete | None = None,
    cutoff: Incomplete | None = None,
): ...
@_dispatch
def edge_connectivity(
    G,
    s: Incomplete | None = None,
    t: Incomplete | None = None,
    flow_func: Incomplete | None = None,
    cutoff: Incomplete | None = None,
): ...
