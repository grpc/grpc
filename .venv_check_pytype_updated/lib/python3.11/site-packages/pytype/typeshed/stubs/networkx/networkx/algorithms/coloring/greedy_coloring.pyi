from _typeshed import Incomplete
from collections.abc import Generator

from networkx.utils.backends import _dispatch

__all__ = [
    "greedy_color",
    "strategy_connected_sequential",
    "strategy_connected_sequential_bfs",
    "strategy_connected_sequential_dfs",
    "strategy_independent_set",
    "strategy_largest_first",
    "strategy_random_sequential",
    "strategy_saturation_largest_first",
    "strategy_smallest_last",
]

@_dispatch
def strategy_largest_first(G, colors): ...
@_dispatch
def strategy_random_sequential(G, colors, seed: Incomplete | None = None): ...
@_dispatch
def strategy_smallest_last(G, colors): ...
@_dispatch
def strategy_independent_set(G, colors) -> Generator[Incomplete, Incomplete, None]: ...
@_dispatch
def strategy_connected_sequential_bfs(G, colors): ...
@_dispatch
def strategy_connected_sequential_dfs(G, colors): ...
@_dispatch
def strategy_connected_sequential(G, colors, traversal: str = "bfs") -> Generator[Incomplete, None, None]: ...
@_dispatch
def strategy_saturation_largest_first(G, colors) -> Generator[Incomplete, None, Incomplete]: ...
@_dispatch
def greedy_color(G, strategy: str = "largest_first", interchange: bool = False): ...

class _Node:
    node_id: Incomplete
    color: int
    adj_list: Incomplete
    adj_color: Incomplete
    def __init__(self, node_id, n) -> None: ...
    def assign_color(self, adj_entry, color) -> None: ...
    def clear_color(self, adj_entry, color) -> None: ...
    def iter_neighbors(self) -> Generator[Incomplete, None, None]: ...
    def iter_neighbors_color(self, color) -> Generator[Incomplete, None, None]: ...

class _AdjEntry:
    node_id: Incomplete
    next: Incomplete
    mate: Incomplete
    col_next: Incomplete
    col_prev: Incomplete
    def __init__(self, node_id) -> None: ...
