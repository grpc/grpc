from _typeshed import Incomplete
from typing import ClassVar

from ..core import IslamicCalendar

class Tunisia(IslamicCalendar):
    include_labour_day: ClassVar[bool]
    include_prophet_birthday: ClassVar[bool]
    include_eid_al_fitr: ClassVar[bool]
    length_eid_al_fitr: int
    include_day_of_sacrifice: ClassVar[bool]
    length_eid_al_adha: int
    include_islamic_new_year: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    WEEKEND_DAYS: Incomplete
    def get_fixed_holidays(self, year): ...
