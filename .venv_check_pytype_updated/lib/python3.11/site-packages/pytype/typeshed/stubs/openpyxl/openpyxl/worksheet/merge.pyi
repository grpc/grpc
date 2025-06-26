from _typeshed import Incomplete, Unused
from typing import ClassVar

from openpyxl.descriptors.serialisable import Serialisable

from .cell_range import CellRange

class MergeCell(CellRange):
    tagname: ClassVar[str]
    # Same as CellRange.coord
    @property
    def ref(self) -> str: ...
    __attrs__: ClassVar[tuple[str, ...]]
    def __init__(self, ref: Incomplete | None = None) -> None: ...
    def __copy__(self): ...

class MergeCells(Serialisable):
    tagname: ClassVar[str]
    # Overwritten by property below
    # count: Integer
    mergeCell: Incomplete
    __elements__: ClassVar[tuple[str, ...]]
    __attrs__: ClassVar[tuple[str, ...]]
    def __init__(self, count: Unused = None, mergeCell=()) -> None: ...
    @property
    def count(self) -> int: ...

class MergedCellRange(CellRange):
    ws: Incomplete
    start_cell: Incomplete
    def __init__(self, worksheet, coord) -> None: ...
    def format(self) -> None: ...
    def __contains__(self, coord: str) -> bool: ...
    def __copy__(self): ...
