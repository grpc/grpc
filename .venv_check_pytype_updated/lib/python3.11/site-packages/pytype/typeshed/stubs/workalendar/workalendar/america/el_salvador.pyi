from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class ElSalvador(WesternCalendar):
    include_labour_day: ClassVar[bool]
    include_holy_thursday: ClassVar[bool]
    include_good_friday: ClassVar[bool]
    include_easter_saturday: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
