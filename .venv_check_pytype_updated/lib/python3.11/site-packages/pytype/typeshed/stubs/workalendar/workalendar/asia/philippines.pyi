from _typeshed import Incomplete
from typing import ClassVar

from ..core import ChineseNewYearCalendar, IslamicMixin, WesternMixin

class Philippines(WesternMixin, IslamicMixin, ChineseNewYearCalendar):
    include_labour_day: ClassVar[bool]
    include_new_years_eve: ClassVar[bool]
    include_holy_thursday: ClassVar[bool]
    holy_thursday_label: ClassVar[str]
    include_good_friday: ClassVar[bool]
    include_easter_saturday: ClassVar[bool]
    include_easter_sunday: ClassVar[bool]
    easter_saturday_label: ClassVar[str]
    include_all_saints: ClassVar[bool]
    include_all_souls: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]
    include_christmas_eve: ClassVar[bool]
    include_eid_al_fitr: ClassVar[bool]
    eid_al_fitr_label: ClassVar[str]
    include_eid_al_adha: ClassVar[bool]
    day_of_sacrifice_label: ClassVar[str]
    WEEKEND_DAYS: Incomplete
    FIXED_HOLIDAYS: Incomplete
