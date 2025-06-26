from _typeshed import Incomplete
from collections.abc import Generator

from networkx.utils.backends import _dispatch

@_dispatch
def graph_edit_distance(
    G1,
    G2,
    node_match: Incomplete | None = None,
    edge_match: Incomplete | None = None,
    node_subst_cost: Incomplete | None = None,
    node_del_cost: Incomplete | None = None,
    node_ins_cost: Incomplete | None = None,
    edge_subst_cost: Incomplete | None = None,
    edge_del_cost: Incomplete | None = None,
    edge_ins_cost: Incomplete | None = None,
    roots: Incomplete | None = None,
    upper_bound: Incomplete | None = None,
    timeout: Incomplete | None = None,
): ...
@_dispatch
def optimal_edit_paths(
    G1,
    G2,
    node_match: Incomplete | None = None,
    edge_match: Incomplete | None = None,
    node_subst_cost: Incomplete | None = None,
    node_del_cost: Incomplete | None = None,
    node_ins_cost: Incomplete | None = None,
    edge_subst_cost: Incomplete | None = None,
    edge_del_cost: Incomplete | None = None,
    edge_ins_cost: Incomplete | None = None,
    upper_bound: Incomplete | None = None,
): ...
@_dispatch
def optimize_graph_edit_distance(
    G1,
    G2,
    node_match: Incomplete | None = None,
    edge_match: Incomplete | None = None,
    node_subst_cost: Incomplete | None = None,
    node_del_cost: Incomplete | None = None,
    node_ins_cost: Incomplete | None = None,
    edge_subst_cost: Incomplete | None = None,
    edge_del_cost: Incomplete | None = None,
    edge_ins_cost: Incomplete | None = None,
    upper_bound: Incomplete | None = None,
) -> Generator[Incomplete, None, None]: ...
@_dispatch
def optimize_edit_paths(
    G1,
    G2,
    node_match: Incomplete | None = None,
    edge_match: Incomplete | None = None,
    node_subst_cost: Incomplete | None = None,
    node_del_cost: Incomplete | None = None,
    node_ins_cost: Incomplete | None = None,
    edge_subst_cost: Incomplete | None = None,
    edge_del_cost: Incomplete | None = None,
    edge_ins_cost: Incomplete | None = None,
    upper_bound: Incomplete | None = None,
    strictly_decreasing: bool = True,
    roots: Incomplete | None = None,
    timeout: Incomplete | None = None,
) -> Generator[Incomplete, None, Incomplete]: ...
@_dispatch
def simrank_similarity(
    G,
    source: Incomplete | None = None,
    target: Incomplete | None = None,
    importance_factor: float = 0.9,
    max_iterations: int = 1000,
    tolerance: float = 0.0001,
): ...
@_dispatch
def panther_similarity(
    G, source, k: int = 5, path_length: int = 5, c: float = 0.5, delta: float = 0.1, eps: Incomplete | None = None
): ...
@_dispatch
def generate_random_paths(
    G, sample_size, path_length: int = 5, index_map: Incomplete | None = None
) -> Generator[Incomplete, None, None]: ...
