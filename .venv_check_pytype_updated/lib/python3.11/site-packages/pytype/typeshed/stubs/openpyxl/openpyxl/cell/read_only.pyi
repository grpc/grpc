from _typeshed import Incomplete
from typing import Final

from openpyxl.cell import _CellValue
from openpyxl.styles.alignment import Alignment
from openpyxl.styles.borders import Border
from openpyxl.styles.cell_style import StyleArray
from openpyxl.styles.fills import Fill
from openpyxl.styles.fonts import Font
from openpyxl.styles.protection import Protection

class ReadOnlyCell:
    parent: Incomplete
    row: Incomplete
    column: Incomplete
    data_type: Incomplete
    def __init__(self, sheet, row, column, value, data_type: str = "n", style_id: int = 0) -> None: ...
    def __eq__(self, other: object) -> bool: ...
    def __ne__(self, other: object) -> bool: ...
    # Same as Cell.coordinate
    # Defined twice in the implementation
    @property
    def coordinate(self) -> str: ...
    # Same as Cell.column_letter
    @property
    def column_letter(self) -> str: ...
    @property
    def style_array(self) -> StyleArray: ...
    @property
    def has_style(self) -> bool: ...
    @property
    def number_format(self) -> str: ...
    @property
    def font(self) -> Font: ...
    @property
    def fill(self) -> Fill: ...
    @property
    def border(self) -> Border: ...
    @property
    def alignment(self) -> Alignment: ...
    @property
    def protection(self) -> Protection: ...
    # Same as Cell.is_date
    @property
    def is_date(self) -> bool: ...
    @property
    def internal_value(self) -> _CellValue | None: ...
    @property
    def value(self) -> _CellValue | None: ...
    @value.setter
    def value(self, value: None) -> None: ...

class EmptyCell:
    value: Incomplete
    is_date: bool
    font: Incomplete
    border: Incomplete
    fill: Incomplete
    number_format: Incomplete
    alignment: Incomplete
    data_type: str

EMPTY_CELL: Final[EmptyCell]
