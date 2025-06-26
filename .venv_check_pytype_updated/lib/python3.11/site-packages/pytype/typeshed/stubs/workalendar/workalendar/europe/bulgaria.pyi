from _typeshed import Incomplete
from collections.abc import Generator
from typing import ClassVar

from ..core import OrthodoxCalendar

class Bulgaria(OrthodoxCalendar):
    FIXED_HOLIDAYS: Incomplete
    include_labour_day: ClassVar[bool]
    labour_day_label: ClassVar[str]
    include_good_friday: ClassVar[bool]
    include_easter_saturday: ClassVar[bool]
    include_easter_sunday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_christmas_eve: ClassVar[bool]
    include_christmas: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    include_orthodox_christmas: ClassVar[bool]
    boxing_day_label: ClassVar[str]

    def get_shifted_holidays(self, days) -> Generator[Incomplete, None, None]: ...
    def get_fixed_holidays(self, year): ...
    def shift_christmas_boxing_days(self, year): ...
    def get_variable_days(self, year): ...
