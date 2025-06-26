from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Cyprus(WesternCalendar):
    include_labour_day: ClassVar[bool]
    include_epiphany: ClassVar[bool]
    include_clean_monday: ClassVar[bool]
    include_good_friday: ClassVar[bool]
    include_easter_saturday: ClassVar[bool]
    include_easter_sunday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_whit_monday: ClassVar[bool]
    whit_monday_label: ClassVar[str]
    include_christmas_eve: ClassVar[bool]
    include_christmas_day: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    def get_variable_days(self, year): ...
