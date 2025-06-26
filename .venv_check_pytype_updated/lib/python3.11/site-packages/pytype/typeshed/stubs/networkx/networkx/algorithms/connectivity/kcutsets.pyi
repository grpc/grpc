from _typeshed import Incomplete
from collections.abc import Generator

from networkx.algorithms.flow import edmonds_karp
from networkx.utils.backends import _dispatch

__all__ = ["all_node_cuts"]

default_flow_func = edmonds_karp

@_dispatch
def all_node_cuts(G, k: Incomplete | None = None, flow_func: Incomplete | None = None) -> Generator[Incomplete, None, None]: ...
