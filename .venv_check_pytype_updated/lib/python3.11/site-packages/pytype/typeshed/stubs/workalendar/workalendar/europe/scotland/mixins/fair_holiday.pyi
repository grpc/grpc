from typing import ClassVar

class FairHoliday:
    include_fair_holiday: ClassVar[bool]
    fair_holiday_label: ClassVar[str]

class FairHolidayLastMondayJune(FairHoliday):
    def get_fair_holiday(self, year): ...

class FairHolidayFirstMondayJuly(FairHoliday):
    def get_fair_holiday(self, year): ...

class FairHolidaySecondMondayJuly(FairHoliday):
    def get_fair_holiday(self, year): ...

class FairHolidayThirdMondayJuly(FairHoliday):
    def get_fair_holiday(self, year): ...

class FairHolidayLastMondayJuly(FairHoliday):
    def get_fair_holiday(self, year): ...

class FairHolidayFourthFridayJuly(FairHoliday):
    def get_fair_holiday(self, year): ...

class FairHolidayFirstMondayAugust(FairHoliday):
    def get_fair_holiday(self, year): ...
