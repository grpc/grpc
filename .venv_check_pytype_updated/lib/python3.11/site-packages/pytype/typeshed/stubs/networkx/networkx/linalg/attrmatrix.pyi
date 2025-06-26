from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def attr_matrix(
    G,
    edge_attr: Incomplete | None = None,
    node_attr: Incomplete | None = None,
    normalized: bool = False,
    rc_order: Incomplete | None = None,
    dtype: Incomplete | None = None,
    order: Incomplete | None = None,
): ...
@_dispatch
def attr_sparse_matrix(
    G,
    edge_attr: Incomplete | None = None,
    node_attr: Incomplete | None = None,
    normalized: bool = False,
    rc_order: Incomplete | None = None,
    dtype: Incomplete | None = None,
): ...
