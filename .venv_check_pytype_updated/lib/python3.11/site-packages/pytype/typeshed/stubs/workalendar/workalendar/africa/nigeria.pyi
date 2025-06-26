from _typeshed import Incomplete
from typing import ClassVar

from ..core import IslamoWesternCalendar

class Nigeria(IslamoWesternCalendar):
    include_labour_day: ClassVar[bool]
    labour_day_label: ClassVar[str]
    include_good_friday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    include_eid_al_fitr: ClassVar[bool]
    include_day_of_sacrifice: ClassVar[bool]
    shift_sunday_holidays: ClassVar[bool]
    shift_new_years_day: ClassVar[bool]
    WEEKEND_DAYS: Incomplete
    FIXED_HOLIDAYS: Incomplete
    def get_fixed_holidays(self, year): ...
