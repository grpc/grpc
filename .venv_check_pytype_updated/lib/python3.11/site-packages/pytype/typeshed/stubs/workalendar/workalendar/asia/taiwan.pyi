from _typeshed import Incomplete
from typing import ClassVar

from ..core import ChineseNewYearCalendar

class Taiwan(ChineseNewYearCalendar):
    FIXED_HOLIDAYS: Incomplete
    include_chinese_new_year_eve: ClassVar[bool]
    include_chinese_second_day: ClassVar[bool]
    def is_working_day(self, day, *args, **kwargs): ...
    def get_variable_days(self, year): ...
