from _typeshed import Incomplete
from typing import ClassVar

from ..core import ChineseNewYearCalendar

holidays: Incomplete
workdays: Incomplete

class China(ChineseNewYearCalendar):
    shift_new_years_day: ClassVar[bool]
    include_chinese_new_year_eve: ClassVar[bool]
    extra_working_days: Incomplete
    def __init__(self, *args, **kwargs) -> None: ...
    def get_calendar_holidays(self, year): ...
    def get_variable_days(self, year): ...
    def is_working_day(self, day, extra_working_days: Incomplete | None = None, extra_holidays: Incomplete | None = None): ...
    def add_working_days(
        self,
        day,
        delta,
        extra_working_days: Incomplete | None = None,
        extra_holidays: Incomplete | None = None,
        keep_datetime: bool = False,
    ): ...
    def sub_working_days(
        self,
        day,
        delta,
        extra_working_days: Incomplete | None = None,
        extra_holidays: Incomplete | None = None,
        keep_datetime: bool = False,
    ): ...
