from collections.abc import Hashable
from typing import TypeVar

from networkx.classes.graph import Graph
from networkx.utils.backends import _dispatch

_G = TypeVar("_G", bound=Graph[Hashable])

@_dispatch
def complement(G): ...
@_dispatch
def reverse(G: _G, copy: bool = True) -> _G: ...
