from _typeshed import Incomplete
from typing import ClassVar

from ..core import ChineseNewYearCalendar, IslamicMixin, WesternMixin

class Singapore(WesternMixin, IslamicMixin, ChineseNewYearCalendar):
    include_labour_day: ClassVar[bool]
    include_good_friday: ClassVar[bool]
    include_eid_al_fitr: ClassVar[bool]
    eid_al_fitr_label: ClassVar[str]
    include_day_of_sacrifice: ClassVar[bool]
    day_of_sacrifice_label: ClassVar[str]
    FIXED_HOLIDAYS: Incomplete
    WEEKEND_DAYS: Incomplete
    DEEPAVALI: Incomplete
    chinese_new_year_label: ClassVar[str]
    include_chinese_second_day: ClassVar[bool]
    chinese_second_day_label: ClassVar[str]
    shift_sunday_holidays: ClassVar[bool]
    def get_variable_days(self, year): ...
