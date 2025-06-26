from _typeshed import Incomplete
from collections.abc import Generator
from typing import ClassVar

__all__ = ["forest_str", "generate_network_text", "write_network_text"]

class _AsciiBaseGlyphs:
    empty: ClassVar[str]
    newtree_last: ClassVar[str]
    newtree_mid: ClassVar[str]
    endof_forest: ClassVar[str]
    within_forest: ClassVar[str]
    within_tree: ClassVar[str]

class AsciiDirectedGlyphs(_AsciiBaseGlyphs):
    last: ClassVar[str]
    mid: ClassVar[str]
    backedge: ClassVar[str]

class AsciiUndirectedGlyphs(_AsciiBaseGlyphs):
    last: ClassVar[str]
    mid: ClassVar[str]
    backedge: ClassVar[str]

class _UtfBaseGlyphs:
    empty: ClassVar[str]
    newtree_last: ClassVar[str]
    newtree_mid: ClassVar[str]
    endof_forest: ClassVar[str]
    within_forest: ClassVar[str]
    within_tree: ClassVar[str]

class UtfDirectedGlyphs(_UtfBaseGlyphs):
    last: ClassVar[str]
    mid: ClassVar[str]
    backedge: ClassVar[str]

class UtfUndirectedGlyphs(_UtfBaseGlyphs):
    last: ClassVar[str]
    mid: ClassVar[str]
    backedge: ClassVar[str]

def generate_network_text(
    graph,
    with_labels: bool = True,
    sources: Incomplete | None = None,
    max_depth: Incomplete | None = None,
    ascii_only: bool = False,
    vertical_chains: bool = False,
) -> Generator[Incomplete, None, Incomplete]: ...
def write_network_text(
    graph,
    path: Incomplete | None = None,
    with_labels: bool = True,
    sources: Incomplete | None = None,
    max_depth: Incomplete | None = None,
    ascii_only: bool = False,
    end: str = "\n",
    vertical_chains=False,
) -> None: ...
def forest_str(
    graph, with_labels: bool = True, sources: Incomplete | None = None, write: Incomplete | None = None, ascii_only: bool = False
): ...
