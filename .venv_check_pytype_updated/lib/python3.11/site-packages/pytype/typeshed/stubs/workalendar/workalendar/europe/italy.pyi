from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Italy(WesternCalendar):
    include_labour_day: ClassVar[bool]
    labour_day_label: ClassVar[str]
    FIXED_HOLIDAYS: Incomplete
    include_immaculate_conception: ClassVar[bool]
    include_epiphany: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    boxing_day_label: ClassVar[str]
