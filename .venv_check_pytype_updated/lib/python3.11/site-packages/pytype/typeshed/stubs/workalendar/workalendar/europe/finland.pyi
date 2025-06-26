from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Finland(WesternCalendar):
    include_epiphany: ClassVar[bool]
    include_good_friday: ClassVar[bool]
    include_easter_sunday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_ascension: ClassVar[bool]
    include_whit_sunday: ClassVar[bool]
    whit_sunday_label: ClassVar[str]
    include_christmas_eve: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    boxing_day_label: ClassVar[str]
    include_labour_day: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    def get_midsummer_eve(self, year): ...
    def get_midsummer_day(self, year): ...
    def get_variable_all_saints(self, year): ...
    def get_variable_days(self, year): ...
