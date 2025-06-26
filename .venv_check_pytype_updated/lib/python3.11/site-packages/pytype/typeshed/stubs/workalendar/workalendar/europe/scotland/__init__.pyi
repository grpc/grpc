from _typeshed import Incomplete
from typing import ClassVar

from ...core import WesternCalendar
from .mixins import (
    AutumnHolidayFirstMondayOctober,
    AutumnHolidayLastMondaySeptember,
    AutumnHolidaySecondMondayOctober,
    AutumnHolidayThirdMondayOctober,
    AyrGoldCup,
    BattleStirlingBridge,
    FairHolidayFirstMondayAugust,
    FairHolidayFirstMondayJuly,
    FairHolidayFourthFridayJuly,
    FairHolidayLastMondayJuly,
    FairHolidayLastMondayJune,
    FairHolidaySecondMondayJuly,
    FairHolidayThirdMondayJuly,
    LateSummer,
    SpringHolidayFirstMondayApril,
    SpringHolidayFirstMondayJune,
    SpringHolidayLastMondayMay,
    SpringHolidaySecondMondayApril,
    SpringHolidayTuesdayAfterFirstMondayMay,
    VictoriaDayFirstMondayJune,
    VictoriaDayFourthMondayMay,
    VictoriaDayLastMondayMay,
)

class Scotland(WesternCalendar):
    FIXED_HOLIDAYS: Incomplete
    include_spring_holiday: ClassVar[bool]
    spring_holiday_label: ClassVar[str]
    include_fair_holiday: ClassVar[bool]
    include_autumn_holiday: ClassVar[bool]
    include_saint_andrew: ClassVar[bool]
    include_victoria_day: ClassVar[bool]
    def __init__(self, *args, **kwargs) -> None: ...
    def get_may_day(self, year): ...
    def get_spring_holiday(self, year) -> None: ...
    def get_fair_holiday(self, year) -> None: ...
    def get_autumn_holiday(self, year) -> None: ...
    def get_victoria_day(self, year) -> None: ...
    def get_variable_days(self, year): ...
    def get_fixed_holidays(self, year): ...

class Aberdeen(FairHolidaySecondMondayJuly, AutumnHolidayLastMondaySeptember, Scotland):
    include_good_friday: ClassVar[bool]

class Angus(SpringHolidaySecondMondayApril, AutumnHolidayLastMondaySeptember, Scotland):
    include_saint_andrew: ClassVar[bool]

class Arbroath(FairHolidayThirdMondayJuly, Scotland): ...

class Ayr(SpringHolidayLastMondayMay, AyrGoldCup, Scotland):
    include_good_friday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]

class CarnoustieMonifieth(SpringHolidayFirstMondayApril, AutumnHolidayFirstMondayOctober, Scotland): ...
class Clydebank(SpringHolidayTuesdayAfterFirstMondayMay, Scotland): ...

class DumfriesGalloway(Scotland):
    include_good_friday: ClassVar[bool]

class Dundee(
    SpringHolidayFirstMondayApril, VictoriaDayLastMondayMay, FairHolidayLastMondayJuly, AutumnHolidayFirstMondayOctober, Scotland
): ...

class EastDunbartonshire(SpringHolidayLastMondayMay, FairHolidayThirdMondayJuly, AutumnHolidayLastMondaySeptember, Scotland):
    include_good_friday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]

class Edinburgh(Scotland):
    include_good_friday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_spring_holiday: ClassVar[bool]
    include_victoria_day: ClassVar[bool]
    include_autumn_holiday: ClassVar[bool]
    def get_spring_holiday(self, year): ...
    def get_victoria_day(self, year): ...
    def get_autumn_holiday(self, year): ...

class Elgin(SpringHolidaySecondMondayApril, FairHolidayLastMondayJune, LateSummer, AutumnHolidayThirdMondayOctober, Scotland): ...

class Falkirk(FairHolidayFirstMondayJuly, BattleStirlingBridge, Scotland):
    include_good_friday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]

class Fife(VictoriaDayFirstMondayJune, FairHolidayThirdMondayJuly, AutumnHolidayThirdMondayOctober, Scotland):
    include_saint_andrew: ClassVar[bool]
    def get_variable_days(self, year): ...

class Galashiels(SpringHolidayFirstMondayJune, Scotland):
    def get_variable_days(self, year): ...

class Glasgow(SpringHolidayLastMondayMay, FairHolidayThirdMondayJuly, AutumnHolidayLastMondaySeptember, Scotland):
    include_easter_monday: ClassVar[bool]

class Hawick(Scotland):
    def get_variable_days(self, year): ...

class Inverclyde(LateSummer, Scotland):
    include_good_friday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    def get_variable_days(self, year): ...

class Inverness(SpringHolidayFirstMondayApril, FairHolidayFirstMondayJuly, AutumnHolidayFirstMondayOctober, Scotland):
    def get_variable_days(self, year): ...

class Kilmarnock(AyrGoldCup, Scotland):
    include_good_friday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]

class Lanark(Scotland):
    def get_variable_days(self, year): ...

class Linlithgow(Scotland):
    def get_variable_days(self, year): ...

class Lochaber(Scotland):
    def get_variable_days(self, year): ...

class NorthLanarkshire(SpringHolidayLastMondayMay, FairHolidayThirdMondayJuly, AutumnHolidayLastMondaySeptember, Scotland):
    include_easter_monday: ClassVar[bool]

class Paisley(VictoriaDayLastMondayMay, FairHolidayFirstMondayAugust, AutumnHolidayLastMondaySeptember, Scotland):
    include_good_friday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]

class Perth(
    SpringHolidayFirstMondayApril, VictoriaDayFourthMondayMay, BattleStirlingBridge, AutumnHolidayFirstMondayOctober, Scotland
): ...

class ScottishBorders(SpringHolidayFirstMondayApril, FairHolidayFourthFridayJuly, AutumnHolidaySecondMondayOctober, Scotland):
    include_saint_andrew: ClassVar[bool]

class SouthLanarkshire(SpringHolidayLastMondayMay, FairHolidayThirdMondayJuly, AutumnHolidayLastMondaySeptember, Scotland):
    include_good_friday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]

class Stirling(SpringHolidayTuesdayAfterFirstMondayMay, BattleStirlingBridge, Scotland):
    include_good_friday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]

class WestDunbartonshire(AutumnHolidayLastMondaySeptember, Scotland):
    include_good_friday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
