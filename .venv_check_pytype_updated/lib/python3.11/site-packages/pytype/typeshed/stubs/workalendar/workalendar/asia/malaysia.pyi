from _typeshed import Incomplete
from typing import ClassVar

from ..core import ChineseNewYearCalendar, IslamicMixin

class Malaysia(IslamicMixin, ChineseNewYearCalendar):
    include_labour_day: ClassVar[bool]
    labour_day_label: ClassVar[str]
    include_nuzul_al_quran: ClassVar[bool]
    include_eid_al_fitr: ClassVar[bool]
    length_eid_al_fitr: int
    eid_al_fitr_label: ClassVar[str]
    include_day_of_sacrifice: ClassVar[bool]
    day_of_sacrifice_label: ClassVar[str]
    include_islamic_new_year: ClassVar[bool]
    include_prophet_birthday: ClassVar[bool]
    WEEKEND_DAYS: Incomplete
    FIXED_HOLIDAYS: Incomplete
    MSIA_DEEPAVALI: Incomplete
    MSIA_THAIPUSAM: Incomplete
    chinese_new_year_label: ClassVar[str]
    include_chinese_second_day: ClassVar[bool]
    chinese_second_day_label: ClassVar[str]
    shift_sunday_holidays: ClassVar[bool]
    def get_variable_days(self, year): ...
