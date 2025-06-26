from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Estonia(WesternCalendar):
    include_good_friday: ClassVar[bool]
    include_easter_sunday: ClassVar[bool]
    include_whit_sunday: ClassVar[bool]
    whit_sunday_label: ClassVar[str]
    include_christmas_eve: ClassVar[bool]
    include_christmas: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    boxing_day_label: ClassVar[str]
    FIXED_HOLIDAYS: Incomplete
