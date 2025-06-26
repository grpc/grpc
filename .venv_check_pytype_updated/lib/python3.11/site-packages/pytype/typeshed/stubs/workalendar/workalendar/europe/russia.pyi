from _typeshed import Incomplete
from typing import ClassVar

from ..core import OrthodoxCalendar

class Russia(OrthodoxCalendar):
    include_labour_day: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    include_christmas: ClassVar[bool]
    covid19_2020_start: Incomplete
    covid19_2020_end: Incomplete
    labour_day_label: ClassVar[str]
    def get_fixed_holidays(self, year): ...
    def get_calendar_holidays(self, year): ...
    def is_working_day(self, day, extra_working_days: Incomplete | None = None, extra_holidays: Incomplete | None = None): ...
