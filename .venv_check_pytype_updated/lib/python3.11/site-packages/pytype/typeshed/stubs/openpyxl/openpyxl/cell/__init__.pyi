from datetime import date, datetime, time, timedelta
from decimal import Decimal
from typing_extensions import TypeAlias

from openpyxl.cell.rich_text import CellRichText
from openpyxl.worksheet.formula import ArrayFormula, DataTableFormula

from .cell import Cell as Cell, MergedCell as MergedCell, WriteOnlyCell as WriteOnlyCell
from .read_only import ReadOnlyCell as ReadOnlyCell

_TimeTypes: TypeAlias = datetime | date | time | timedelta
_CellValue: TypeAlias = (  # noqa: Y047 # Used in other modules
    # if numpy is installed also numpy bool and number types
    bool
    | float
    | Decimal
    | str
    | CellRichText
    | _TimeTypes
    | DataTableFormula
    | ArrayFormula
)
