from typing import ClassVar

class SpringHoliday:
    include_spring_holiday: ClassVar[bool]

class SpringHolidayFirstMondayApril(SpringHoliday):
    def get_spring_holiday(self, year): ...

class SpringHolidaySecondMondayApril(SpringHoliday):
    def get_spring_holiday(self, year): ...

class SpringHolidayTuesdayAfterFirstMondayMay(SpringHoliday):
    def get_spring_holiday(self, year): ...

class SpringHolidayLastMondayMay(SpringHoliday):
    def get_spring_holiday(self, year): ...

class SpringHolidayFirstMondayJune(SpringHoliday):
    def get_spring_holiday(self, year): ...
