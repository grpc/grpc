from _typeshed import Incomplete
from typing import ClassVar

from ..core import OrthodoxCalendar

class Belarus(OrthodoxCalendar):
    include_labour_day: ClassVar[bool]
    include_christmas: ClassVar[bool]
    christmas_day_label: ClassVar[str]
    orthodox_christmas_day_label: ClassVar[str]
    FIXED_HOLIDAYS: Incomplete
    def get_radonitsa(self, year): ...
    def get_variable_days(self, year): ...
