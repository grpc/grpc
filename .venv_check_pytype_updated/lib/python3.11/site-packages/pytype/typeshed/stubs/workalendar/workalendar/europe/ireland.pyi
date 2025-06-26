from typing import ClassVar

from ..core import WesternCalendar

class Ireland(WesternCalendar):
    include_easter_monday: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    boxing_day_label: ClassVar[str]
    shift_new_years_day: ClassVar[bool]
    def get_june_holiday(self, year): ...
    def get_august_holiday(self, year): ...
    include_whit_monday: ClassVar[bool]
    def get_variable_days(self, year): ...
