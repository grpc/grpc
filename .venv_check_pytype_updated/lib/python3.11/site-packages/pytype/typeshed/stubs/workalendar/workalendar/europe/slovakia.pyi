from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Slovakia(WesternCalendar):
    include_epiphany: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_good_friday: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_christmas_eve: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    boxing_day_label: ClassVar[str]
    include_labour_day: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
