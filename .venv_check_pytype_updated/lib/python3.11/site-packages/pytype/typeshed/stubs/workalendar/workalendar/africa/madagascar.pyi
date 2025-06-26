from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Madagascar(WesternCalendar):
    FIXED_HOLIDAYS: Incomplete
    include_labour_day: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_ascension: ClassVar[bool]
    include_whit_monday: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_all_saints: ClassVar[bool]
