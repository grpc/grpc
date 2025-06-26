from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Slovenia(WesternCalendar):
    include_easter_sunday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_whit_sunday: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_christmas: ClassVar[bool]
    include_labour_day: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    def get_variable_days(self, year): ...
