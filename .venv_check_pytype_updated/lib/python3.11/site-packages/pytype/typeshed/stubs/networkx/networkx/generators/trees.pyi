from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def prefix_tree(paths): ...
@_dispatch
def prefix_tree_recursive(paths): ...
@_dispatch
def random_tree(n, seed: Incomplete | None = None, create_using: Incomplete | None = None): ...
