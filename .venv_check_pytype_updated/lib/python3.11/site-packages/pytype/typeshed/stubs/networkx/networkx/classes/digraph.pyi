from _typeshed import Incomplete
from collections.abc import Iterator

from networkx.classes.coreviews import AdjacencyView
from networkx.classes.graph import Graph, _Node
from networkx.classes.reportviews import (
    InDegreeView,
    InEdgeView,
    InMultiDegreeView,
    OutDegreeView,
    OutEdgeView,
    OutMultiDegreeView,
)

class DiGraph(Graph[_Node]):
    succ: AdjacencyView[_Node, _Node, dict[str, Incomplete]]
    pred: AdjacencyView[_Node, _Node, dict[str, Incomplete]]
    def has_successor(self, u: _Node, v: _Node) -> bool: ...
    def has_predecessor(self, u: _Node, v: _Node) -> bool: ...
    def successors(self, n: _Node) -> Iterator[_Node]: ...
    def predecessors(self, n: _Node) -> Iterator[_Node]: ...
    in_edges: InEdgeView[_Node]
    in_degree: InDegreeView[_Node] | InMultiDegreeView[_Node]  # ugly hack to make MultiDiGraph work
    out_edges: OutEdgeView[_Node]
    out_degree: OutDegreeView[_Node] | OutMultiDegreeView[_Node]  # ugly hack to make MultiDiGraph work
    def to_undirected(self, reciprocal: bool = False, as_view: bool = False): ...  # type: ignore[override]  # Has an additional `reciprocal` keyword argument
    def reverse(self, copy: bool = True) -> DiGraph[_Node]: ...
    def copy(self, as_view: bool = False) -> DiGraph[_Node]: ...
