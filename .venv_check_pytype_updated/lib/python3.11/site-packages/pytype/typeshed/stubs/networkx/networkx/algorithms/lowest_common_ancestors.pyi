from _typeshed import Incomplete
from collections.abc import Generator

from networkx.utils.backends import _dispatch

@_dispatch
def all_pairs_lowest_common_ancestor(G, pairs: Incomplete | None = None): ...
@_dispatch
def lowest_common_ancestor(G, node1, node2, default: Incomplete | None = None): ...
@_dispatch
def tree_all_pairs_lowest_common_ancestor(
    G, root: Incomplete | None = None, pairs: Incomplete | None = None
) -> Generator[Incomplete, None, None]: ...
