from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def biadjacency_matrix(
    G,
    row_order,
    column_order: Incomplete | None = None,
    dtype: Incomplete | None = None,
    weight: str = "weight",
    format: str = "csr",
): ...
@_dispatch
def from_biadjacency_matrix(A, create_using: Incomplete | None = None, edge_attribute: str = "weight"): ...
