from _typeshed import Incomplete
from collections.abc import Iterator
from dataclasses import dataclass

from networkx.classes.graph import _Node
from networkx.classes.multidigraph import MultiDiGraph
from networkx.utils.backends import _dispatch

__all__ = [
    "branching_weight",
    "greedy_branching",
    "maximum_branching",
    "minimum_branching",
    "maximum_spanning_arborescence",
    "minimum_spanning_arborescence",
    "ArborescenceIterator",
    "Edmonds",
]

@_dispatch
def branching_weight(G, attr: str = "weight", default: float = 1): ...
@_dispatch
def greedy_branching(G, attr: str = "weight", default: float = 1, kind: str = "max", seed: Incomplete | None = None): ...

class MultiDiGraph_EdgeKey(MultiDiGraph[_Node]):
    edge_index: Incomplete
    def __init__(self, incoming_graph_data: Incomplete | None = None, **attr) -> None: ...
    def remove_node(self, n) -> None: ...
    def remove_nodes_from(self, nbunch) -> None: ...
    def add_edge(self, u_for_edge, v_for_edge, key_for_edge, **attr) -> None: ...  # type: ignore[override]  # Graph.add_edge won't accept a `key_for_edge` keyword argument.
    def add_edges_from(self, ebunch_to_add, **attr) -> None: ...
    def remove_edge_with_key(self, key) -> None: ...
    def remove_edges_from(self, ebunch) -> None: ...

class Edmonds:
    G_original: Incomplete
    store: bool
    edges: Incomplete
    template: Incomplete
    def __init__(self, G, seed: Incomplete | None = None) -> None: ...
    def find_optimum(
        self,
        attr: str = "weight",
        default: float = 1,
        kind: str = "max",
        style: str = "branching",
        preserve_attrs: bool = False,
        partition: Incomplete | None = None,
        seed: Incomplete | None = None,
    ): ...

@_dispatch
def maximum_branching(
    G, attr: str = "weight", default: float = 1, preserve_attrs: bool = False, partition: Incomplete | None = None
): ...
@_dispatch
def minimum_branching(
    G, attr: str = "weight", default: float = 1, preserve_attrs: bool = False, partition: Incomplete | None = None
): ...
@_dispatch
def maximum_spanning_arborescence(
    G, attr: str = "weight", default: float = 1, preserve_attrs: bool = False, partition: Incomplete | None = None
): ...
@_dispatch
def minimum_spanning_arborescence(
    G, attr: str = "weight", default: float = 1, preserve_attrs: bool = False, partition: Incomplete | None = None
): ...

class ArborescenceIterator:
    @dataclass
    class Partition:
        mst_weight: float
        partition_dict: dict[Incomplete, Incomplete]

    G: Incomplete
    weight: Incomplete
    minimum: Incomplete
    method: Incomplete
    partition_key: str
    init_partition: Incomplete
    def __init__(self, G, weight: str = "weight", minimum: bool = True, init_partition: Incomplete | None = None) -> None: ...
    partition_queue: Incomplete
    def __iter__(self) -> Iterator[Incomplete]: ...
    def __next__(self): ...
