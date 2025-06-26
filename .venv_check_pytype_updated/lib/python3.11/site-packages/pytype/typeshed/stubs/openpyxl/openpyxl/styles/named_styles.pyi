from _typeshed import ConvertibleToInt, Incomplete, Unused
from collections.abc import Iterator
from typing import ClassVar, Literal

from openpyxl.descriptors.base import Bool, Integer, String, Typed, _ConvertibleToBool
from openpyxl.descriptors.excel import ExtensionList
from openpyxl.descriptors.serialisable import Serialisable
from openpyxl.styles.alignment import Alignment
from openpyxl.styles.borders import Border
from openpyxl.styles.cell_style import CellStyle, StyleArray
from openpyxl.styles.fills import Fill
from openpyxl.styles.fonts import Font
from openpyxl.styles.protection import Protection
from openpyxl.workbook.workbook import Workbook

class NamedStyle(Serialisable):
    font: Typed[Font, Literal[False]]
    fill: Typed[Fill, Literal[False]]
    border: Typed[Border, Literal[False]]
    alignment: Typed[Alignment, Literal[False]]
    number_format: Incomplete
    protection: Typed[Protection, Literal[False]]
    builtinId: Integer[Literal[True]]
    hidden: Bool[Literal[True]]
    # Overwritten by property below
    # xfId: Integer
    name: String[Literal[False]]
    def __init__(
        self,
        name: str = "Normal",
        font: Font | None = None,
        fill: Fill | None = None,
        border: Border | None = None,
        alignment: Alignment | None = None,
        number_format: Incomplete | None = None,
        protection: Protection | None = None,
        builtinId: ConvertibleToInt | None = None,
        hidden: _ConvertibleToBool | None = False,
        xfId: Unused = None,
    ) -> None: ...
    def __setattr__(self, attr: str, value) -> None: ...
    def __iter__(self) -> Iterator[tuple[str, str]]: ...
    @property
    def xfId(self) -> int | None: ...
    def bind(self, wb: Workbook) -> None: ...
    def as_tuple(self) -> StyleArray: ...
    def as_xf(self) -> CellStyle: ...
    def as_name(self) -> _NamedCellStyle: ...

class NamedStyleList(list[NamedStyle]):
    @property
    def names(self) -> list[str]: ...
    def __getitem__(self, key: int | str) -> NamedStyle: ...  # type: ignore[override]
    def append(self, style: NamedStyle) -> None: ...

class _NamedCellStyle(Serialisable):
    tagname: ClassVar[str]
    name: String[Literal[False]]
    xfId: Integer[Literal[False]]
    builtinId: Integer[Literal[True]]
    iLevel: Integer[Literal[True]]
    hidden: Bool[Literal[True]]
    customBuiltin: Bool[Literal[True]]
    extLst: Typed[ExtensionList, Literal[True]]
    __elements__: ClassVar[tuple[str, ...]]
    def __init__(
        self,
        name: str,
        xfId: ConvertibleToInt,
        builtinId: ConvertibleToInt | None = None,
        iLevel: ConvertibleToInt | None = None,
        hidden: _ConvertibleToBool | None = None,
        customBuiltin: _ConvertibleToBool | None = None,
        extLst: Unused = None,
    ) -> None: ...

class _NamedCellStyleList(Serialisable):
    tagname: ClassVar[str]
    # Overwritten by property below
    # count: Integer
    cellStyle: Incomplete
    __attrs__: ClassVar[tuple[str, ...]]
    def __init__(self, count: Unused = None, cellStyle=()) -> None: ...
    @property
    def count(self) -> int: ...
    @property
    def names(self) -> NamedStyleList: ...
