from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Latvia(WesternCalendar):
    include_labour_day: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    include_good_friday: ClassVar[bool]
    include_easter_sunday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_christmas_eve: ClassVar[bool]
    include_christmas: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    def get_independence_days(self, year): ...
    def get_republic_days(self, year): ...
    def get_variable_days(self, year): ...
