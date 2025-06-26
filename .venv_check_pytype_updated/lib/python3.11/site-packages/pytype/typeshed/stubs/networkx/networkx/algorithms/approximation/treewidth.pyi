from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

__all__ = ["treewidth_min_degree", "treewidth_min_fill_in"]

@_dispatch
def treewidth_min_degree(G): ...
@_dispatch
def treewidth_min_fill_in(G): ...

class MinDegreeHeuristic:
    count: Incomplete
    def __init__(self, graph) -> None: ...
    def best_node(self, graph): ...
