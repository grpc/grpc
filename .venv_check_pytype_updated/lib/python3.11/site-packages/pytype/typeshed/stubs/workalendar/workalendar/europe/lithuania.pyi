from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Lithuania(WesternCalendar):
    include_labour_day: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    include_easter_sunday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_christmas_eve: ClassVar[bool]
    include_christmas: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    boxing_day_label: ClassVar[str]
    def get_mothers_day(self, year): ...
    def get_fathers_day(self, year): ...
    include_all_souls: ClassVar[bool]
    def get_variable_days(self, year): ...
