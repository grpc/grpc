from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Barbados(WesternCalendar):
    include_labour_day: ClassVar[bool]
    include_good_friday: ClassVar[bool]
    include_easter_sunday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_whit_monday: ClassVar[bool]
    non_computable_holiday_dict: Incomplete
    FIXED_HOLIDAYS: Incomplete
    def get_kadooment_day(self, year): ...
    def get_emancipation_day(self, year): ...
    def get_variable_days(self, year): ...
    def non_computable_holiday(self, year): ...
    def get_fixed_holidays(self, year): ...
