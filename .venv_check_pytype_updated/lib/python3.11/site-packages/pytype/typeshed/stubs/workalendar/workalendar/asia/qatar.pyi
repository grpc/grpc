from _typeshed import Incomplete
from typing import ClassVar

from ..core import IslamicCalendar

class Qatar(IslamicCalendar):
    include_new_years_day: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    include_start_ramadan: ClassVar[bool]
    include_eid_al_fitr: ClassVar[bool]
    length_eid_al_fitr: int
    include_eid_al_adha: ClassVar[bool]
    length_eid_al_adha: int
