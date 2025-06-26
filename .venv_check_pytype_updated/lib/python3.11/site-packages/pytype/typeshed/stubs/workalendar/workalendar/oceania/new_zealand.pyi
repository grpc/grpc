from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class NewZealand(WesternCalendar):
    include_good_friday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    def get_queens_birthday(self, year): ...
    def get_labour_day(self, year): ...
    def get_variable_days(self, year): ...
