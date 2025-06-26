from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Luxembourg(WesternCalendar):
    include_easter_monday: ClassVar[bool]
    include_ascension: ClassVar[bool]
    include_whit_monday: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    include_labour_day: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    def get_fixed_holidays(self, year): ...
