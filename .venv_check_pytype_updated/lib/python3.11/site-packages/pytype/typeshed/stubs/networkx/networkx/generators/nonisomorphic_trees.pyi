from _typeshed import Incomplete
from collections.abc import Generator

from networkx.utils.backends import _dispatch

@_dispatch
def nonisomorphic_trees(order, create: str = "graph") -> Generator[Incomplete, None, None]: ...
@_dispatch
def number_of_nonisomorphic_trees(order): ...
