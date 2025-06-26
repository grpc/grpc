from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Portugal(WesternCalendar):
    include_good_friday: ClassVar[bool]
    include_easter_sunday: ClassVar[bool]
    include_christmas: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]
    immaculate_conception_label: ClassVar[str]
    include_labour_day: ClassVar[bool]
    labour_day_label: ClassVar[str]
    FIXED_HOLIDAYS: Incomplete
    def get_fixed_holidays(self, year): ...
    def get_variable_days(self, year): ...
