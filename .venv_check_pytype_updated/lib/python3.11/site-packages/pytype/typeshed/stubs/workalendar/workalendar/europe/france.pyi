from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class France(WesternCalendar):
    include_easter_monday: ClassVar[bool]
    include_ascension: ClassVar[bool]
    include_whit_monday: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_labour_day: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete

class FranceAlsaceMoselle(France):
    include_good_friday: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
