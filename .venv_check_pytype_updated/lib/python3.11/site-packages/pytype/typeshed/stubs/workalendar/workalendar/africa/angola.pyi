from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Angola(WesternCalendar):
    include_labour_day: ClassVar[bool]
    labour_day_label: ClassVar[str]
    include_fat_tuesday: ClassVar[bool]
    fat_tuesday_label: ClassVar[str]
    include_good_friday: ClassVar[bool]
    include_easter_sunday: ClassVar[bool]
    include_christmas: ClassVar[bool]
    include_all_souls: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
