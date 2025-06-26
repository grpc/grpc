from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class SaoTomeAndPrincipe(WesternCalendar):
    FIXED_HOLIDAYS: Incomplete
    include_labour_day: ClassVar[bool]
    include_all_saints: ClassVar[bool]
