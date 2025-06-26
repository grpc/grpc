from _typeshed import Incomplete
from collections.abc import Hashable
from typing import TypeVar

from networkx.classes.digraph import DiGraph
from networkx.utils.backends import _dispatch

@_dispatch
def disjoint_union(G, H): ...
@_dispatch
def intersection(G, H): ...
@_dispatch
def difference(G, H): ...
@_dispatch
def symmetric_difference(G, H): ...

_X = TypeVar("_X", bound=Hashable, covariant=True)
_Y = TypeVar("_Y", bound=Hashable, covariant=True)
# GT = TypeVar('GT', bound=Graph[_Node])
# TODO: This does not handle the cases when graphs of different types are passed which is allowed

@_dispatch
def compose(G: DiGraph[_X], H: DiGraph[_Y]) -> DiGraph[_X | _Y]: ...
@_dispatch
def union(G: DiGraph[_X], H: DiGraph[_Y], rename: Incomplete = ()) -> DiGraph[_X | _Y]: ...
