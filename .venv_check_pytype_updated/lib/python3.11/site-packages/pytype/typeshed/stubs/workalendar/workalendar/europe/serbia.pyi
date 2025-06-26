from _typeshed import Incomplete
from typing import ClassVar

from ..core import OrthodoxCalendar

class Serbia(OrthodoxCalendar):
    FIXED_HOLIDAYS: Incomplete
    include_labour_day: ClassVar[bool]
    include_good_friday: ClassVar[bool]
    include_easter_sunday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_christmas: ClassVar[bool]
