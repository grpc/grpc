from _typeshed import Incomplete
from collections.abc import Callable, Hashable
from io import TextIOBase
from typing_extensions import TypeAlias

from networkx.classes.graph import Graph, _Node
from networkx.utils.backends import _dispatch

# from pygraphviz.agraph import AGraph as _AGraph
_AGraph: TypeAlias = Incomplete

@_dispatch
def from_agraph(A, create_using: Incomplete | None = None) -> Graph[Incomplete]: ...
def to_agraph(N: Graph[Hashable]) -> _AGraph: ...
def write_dot(G: Graph[Hashable], path: str | TextIOBase) -> None: ...
@_dispatch
def read_dot(path: str | TextIOBase) -> Graph[Incomplete]: ...
def graphviz_layout(
    G: Graph[_Node], prog: str = "neato", root: str | None = None, args: str = ""
) -> dict[_Node, tuple[float, float]]: ...

pygraphviz_layout = graphviz_layout

def view_pygraphviz(
    G: Graph[_Node],
    # From implementation looks like Callable could return object since it's always immediatly stringified
    # But judging by documentation this seems like an extra runtime safty thing and not intended
    # Leaving as str unless anyone reports a valid use-case
    edgelabel: str | Callable[[_Node], str] | None = None,
    prog: str = "dot",
    args: str = "",
    suffix: str = "",
    path: str | None = None,
    show: bool = True,
): ...
