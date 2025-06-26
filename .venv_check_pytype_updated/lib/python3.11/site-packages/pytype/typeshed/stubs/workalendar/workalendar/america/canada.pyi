from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Canada(WesternCalendar):
    FIXED_HOLIDAYS: Incomplete
    shift_new_years_day: ClassVar[bool]
    def get_variable_days(self, year): ...

class LateFamilyDayMixin:
    def get_family_day(self, year, label: str = "Family Day"): ...

class VictoriaDayMixin:
    def get_victoria_day(self, year): ...

class AugustCivicHolidayMixin:
    def get_civic_holiday(self, year, label: str = "Civic Holiday"): ...

class ThanksgivingMixin:
    def get_thanksgiving(self, year): ...

class BoxingDayMixin:
    def get_boxing_day(self, year): ...

class StJeanBaptisteMixin:
    def get_st_jean(self, year): ...

class RemembranceDayShiftMixin:
    def get_remembrance_day(self, year): ...

class Ontario(BoxingDayMixin, ThanksgivingMixin, VictoriaDayMixin, LateFamilyDayMixin, AugustCivicHolidayMixin, Canada):
    include_good_friday: ClassVar[bool]
    def get_variable_days(self, year): ...

class Quebec(VictoriaDayMixin, StJeanBaptisteMixin, ThanksgivingMixin, Canada):
    include_easter_monday: ClassVar[bool]
    def get_variable_days(self, year): ...

class BritishColumbia(VictoriaDayMixin, AugustCivicHolidayMixin, ThanksgivingMixin, Canada):
    include_good_friday: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    def get_family_day(self, year): ...
    def get_variable_days(self, year): ...

class Alberta(LateFamilyDayMixin, VictoriaDayMixin, ThanksgivingMixin, Canada):
    include_good_friday: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    def get_variable_days(self, year): ...

class Saskatchewan(
    LateFamilyDayMixin, VictoriaDayMixin, RemembranceDayShiftMixin, AugustCivicHolidayMixin, ThanksgivingMixin, Canada
):
    include_good_friday: ClassVar[bool]
    def get_variable_days(self, year): ...

class Manitoba(LateFamilyDayMixin, VictoriaDayMixin, AugustCivicHolidayMixin, ThanksgivingMixin, Canada):
    include_good_friday: ClassVar[bool]
    def get_variable_days(self, year): ...

class NewBrunswick(AugustCivicHolidayMixin, Canada):
    FIXED_HOLIDAYS: Incomplete
    include_good_friday: ClassVar[bool]
    def get_variable_days(self, year): ...

class NovaScotia(RemembranceDayShiftMixin, LateFamilyDayMixin, Canada):
    include_good_friday: ClassVar[bool]
    def get_variable_days(self, year): ...

class PrinceEdwardIsland(LateFamilyDayMixin, RemembranceDayShiftMixin, Canada):
    include_good_friday: ClassVar[bool]
    def get_variable_days(self, year): ...

class Newfoundland(Canada):
    include_good_friday: ClassVar[bool]

class Yukon(VictoriaDayMixin, ThanksgivingMixin, Canada):
    FIXED_HOLIDAYS: Incomplete
    include_good_friday: ClassVar[bool]
    def get_variable_days(self, year): ...

class NorthwestTerritories(RemembranceDayShiftMixin, VictoriaDayMixin, ThanksgivingMixin, Canada):
    FIXED_HOLIDAYS: Incomplete
    include_good_friday: ClassVar[bool]
    def get_variable_days(self, year): ...

class Nunavut(VictoriaDayMixin, ThanksgivingMixin, RemembranceDayShiftMixin, Canada):
    include_good_friday: ClassVar[bool]
    def get_variable_days(self, year): ...
