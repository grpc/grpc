from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def weisfeiler_lehman_graph_hash(
    G, edge_attr: Incomplete | None = None, node_attr: Incomplete | None = None, iterations: int = 3, digest_size: int = 16
): ...
@_dispatch
def weisfeiler_lehman_subgraph_hashes(
    G, edge_attr: Incomplete | None = None, node_attr: Incomplete | None = None, iterations: int = 3, digest_size: int = 16
): ...
