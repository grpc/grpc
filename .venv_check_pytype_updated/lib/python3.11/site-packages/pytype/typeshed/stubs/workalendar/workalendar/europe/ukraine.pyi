from _typeshed import Incomplete
from typing import ClassVar

from ..core import OrthodoxCalendar

class Ukraine(OrthodoxCalendar):
    shift_sunday_holidays: ClassVar[bool]
    shift_new_years_day: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    include_labour_day: ClassVar[bool]
    labour_day_label: ClassVar[str]
    include_christmas: ClassVar[bool]
    include_good_friday: ClassVar[bool]
    include_easter_sunday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_whit_monday: ClassVar[bool]
    def get_variable_days(self, year): ...
