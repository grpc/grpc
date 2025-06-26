from _typeshed import Incomplete
from typing import ClassVar

from ..core import IslamoWesternCalendar

class Benin(IslamoWesternCalendar):
    include_labour_day: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_ascension: ClassVar[bool]
    include_whit_monday: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_prophet_birthday: ClassVar[bool]
    include_eid_al_fitr: ClassVar[bool]
    include_day_of_sacrifice: ClassVar[bool]
    include_day_of_sacrifice_label: ClassVar[str]
    FIXED_HOLIDAYS: Incomplete
    WEEKEND_DAYS: Incomplete
