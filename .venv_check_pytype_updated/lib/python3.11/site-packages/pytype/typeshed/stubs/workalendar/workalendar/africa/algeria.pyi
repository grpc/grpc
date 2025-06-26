from _typeshed import Incomplete
from typing import ClassVar

from ..core import IslamicCalendar

class Algeria(IslamicCalendar):
    include_labour_day: ClassVar[bool]
    include_prophet_birthday: ClassVar[bool]
    include_eid_al_fitr: ClassVar[bool]
    include_day_of_sacrifice: ClassVar[bool]
    include_islamic_new_year: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    ISLAMIC_HOLIDAYS: Incomplete
