from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class EuropeanCentralBank(WesternCalendar):
    include_labour_day: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    include_good_friday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
