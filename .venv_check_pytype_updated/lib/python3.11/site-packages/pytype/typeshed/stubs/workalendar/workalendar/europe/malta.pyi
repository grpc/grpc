from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Malta(WesternCalendar):
    include_good_friday: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]
    include_christmas: ClassVar[bool]
    include_labour_day: ClassVar[bool]
    labour_day_label: ClassVar[str]
    FIXED_HOLIDAYS: Incomplete
