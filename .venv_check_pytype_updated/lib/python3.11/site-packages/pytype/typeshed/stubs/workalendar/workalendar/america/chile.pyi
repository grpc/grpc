from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Chile(WesternCalendar):
    FIXED_HOLIDAYS: Incomplete
    include_labour_day: ClassVar[bool]
    include_good_friday: ClassVar[bool]
    include_easter_saturday: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]
    def get_variable_days(self, year): ...
