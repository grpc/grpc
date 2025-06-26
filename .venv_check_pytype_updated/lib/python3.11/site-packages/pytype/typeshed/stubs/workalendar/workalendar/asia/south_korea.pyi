from _typeshed import Incomplete
from typing import ClassVar

from ..core import ChineseNewYearCalendar

class SouthKorea(ChineseNewYearCalendar):
    FIXED_HOLIDAYS: Incomplete
    chinese_new_year_label: ClassVar[str]
    include_chinese_new_year_eve: ClassVar[bool]
    chinese_new_year_eve_label: ClassVar[str]
    include_chinese_second_day: ClassVar[bool]
    chinese_second_day_label: ClassVar[str]
    def get_variable_days(self, year): ...
