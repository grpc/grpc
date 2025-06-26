from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class MarshallIslands(WesternCalendar):
    FIXED_HOLIDAYS: Incomplete
    include_good_friday: ClassVar[bool]
    def get_variable_days(self, year): ...
