from _typeshed import Incomplete
from typing import ClassVar

from ..core import OrthodoxCalendar

class Georgia(OrthodoxCalendar):
    include_christmas: ClassVar[bool]
    include_christmas_eve: ClassVar[bool]
    include_new_years_day: ClassVar[bool]
    include_orthodox_christmas: ClassVar[bool]
    include_epiphany: ClassVar[bool]
    include_good_friday: ClassVar[bool]
    include_easter_saturday: ClassVar[bool]
    include_easter_sunday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
