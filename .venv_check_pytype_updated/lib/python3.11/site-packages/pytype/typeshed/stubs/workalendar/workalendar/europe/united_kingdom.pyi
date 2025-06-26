from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class UnitedKingdom(WesternCalendar):
    include_good_friday: ClassVar[bool]
    include_easter_sunday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    shift_new_years_day: ClassVar[bool]
    non_computable_holiday_dict: Incomplete
    def get_early_may_bank_holiday(self, year): ...
    def get_spring_bank_holiday(self, year): ...
    def get_late_summer_bank_holiday(self, year): ...
    def non_computable_holiday(self, year): ...
    def get_variable_days(self, year): ...

class UnitedKingdomNorthernIreland(UnitedKingdom):
    def get_variable_days(self, year): ...
