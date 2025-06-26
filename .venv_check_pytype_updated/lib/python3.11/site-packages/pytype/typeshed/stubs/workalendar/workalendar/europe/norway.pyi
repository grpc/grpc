from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Norway(WesternCalendar):
    include_holy_thursday: ClassVar[bool]
    include_good_friday: ClassVar[bool]
    include_easter_sunday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_ascension: ClassVar[bool]
    include_whit_monday: ClassVar[bool]
    include_whit_sunday: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    boxing_day_label: ClassVar[str]
    include_labour_day: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
