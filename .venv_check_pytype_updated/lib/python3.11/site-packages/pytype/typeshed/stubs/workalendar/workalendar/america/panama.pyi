from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Panama(WesternCalendar):
    include_labour_day: ClassVar[bool]
    include_good_friday: ClassVar[bool]
    include_easter_saturday: ClassVar[bool]
    include_easter_sunday: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    def get_variable_days(self, year): ...
