from .autumn_holiday import (
    AutumnHolidayFirstMondayOctober as AutumnHolidayFirstMondayOctober,
    AutumnHolidayLastMondaySeptember as AutumnHolidayLastMondaySeptember,
    AutumnHolidaySecondMondayOctober as AutumnHolidaySecondMondayOctober,
    AutumnHolidayThirdMondayOctober as AutumnHolidayThirdMondayOctober,
)
from .fair_holiday import (
    FairHolidayFirstMondayAugust as FairHolidayFirstMondayAugust,
    FairHolidayFirstMondayJuly as FairHolidayFirstMondayJuly,
    FairHolidayFourthFridayJuly as FairHolidayFourthFridayJuly,
    FairHolidayLastMondayJuly as FairHolidayLastMondayJuly,
    FairHolidayLastMondayJune as FairHolidayLastMondayJune,
    FairHolidaySecondMondayJuly as FairHolidaySecondMondayJuly,
    FairHolidayThirdMondayJuly as FairHolidayThirdMondayJuly,
)
from .spring_holiday import (
    SpringHolidayFirstMondayApril as SpringHolidayFirstMondayApril,
    SpringHolidayFirstMondayJune as SpringHolidayFirstMondayJune,
    SpringHolidayLastMondayMay as SpringHolidayLastMondayMay,
    SpringHolidaySecondMondayApril as SpringHolidaySecondMondayApril,
    SpringHolidayTuesdayAfterFirstMondayMay as SpringHolidayTuesdayAfterFirstMondayMay,
)
from .victoria_day import (
    VictoriaDayFirstMondayJune as VictoriaDayFirstMondayJune,
    VictoriaDayFourthMondayMay as VictoriaDayFourthMondayMay,
    VictoriaDayLastMondayMay as VictoriaDayLastMondayMay,
)

class LateSummer:
    def get_variable_days(self, year): ...

class BattleStirlingBridge:
    def get_variable_days(self, year): ...

class AyrGoldCup:
    def get_variable_days(self, year): ...

# Names in __all__ with no definition:
#   VictoriaDayTuesdayAfterFirstMondayMay
