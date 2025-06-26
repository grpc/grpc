from _typeshed import Incomplete
from collections.abc import Generator
from typing import ClassVar

from ..core import IslamoWesternCalendar

class Kenya(IslamoWesternCalendar):
    include_labour_day: ClassVar[bool]
    include_good_friday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_eid_al_fitr: ClassVar[bool]
    include_day_of_sacrifice: ClassVar[bool]
    shift_sunday_holidays: ClassVar[bool]
    WEEKEND_DAYS: Incomplete
    FIXED_HOLIDAYS: Incomplete
    def get_fixed_holidays(self, year): ...
    def get_shifted_holidays(self, dates) -> Generator[Incomplete, None, None]: ...
    def get_calendar_holidays(self, year): ...
