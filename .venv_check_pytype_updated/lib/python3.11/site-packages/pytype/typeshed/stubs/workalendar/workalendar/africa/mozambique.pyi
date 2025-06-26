from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Mozambique(WesternCalendar):
    FIXED_HOLIDAYS: Incomplete
    include_labour_day: ClassVar[bool]
    include_good_friday: ClassVar[bool]
    include_christmas: ClassVar[bool]
