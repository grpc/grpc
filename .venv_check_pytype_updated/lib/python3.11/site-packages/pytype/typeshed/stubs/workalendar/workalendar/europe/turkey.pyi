from _typeshed import Incomplete
from typing import ClassVar

from ..core import IslamicCalendar

class Turkey(IslamicCalendar):
    shift_new_years_day: ClassVar[bool]
    WEEKEND_DAYS: Incomplete
    include_eid_al_fitr: ClassVar[bool]
    length_eid_al_fitr: int
    include_eid_al_adha: ClassVar[bool]
    length_eid_al_adha: int
    include_labour_day: ClassVar[bool]
    labour_day_label: ClassVar[str]
    FIXED_HOLIDAYS: Incomplete
    def get_delta_islamic_holidays(self, year): ...
