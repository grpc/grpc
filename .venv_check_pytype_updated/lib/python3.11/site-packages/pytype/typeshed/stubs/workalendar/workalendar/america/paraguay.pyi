from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Paraguay(WesternCalendar):
    FIXED_HOLIDAYS: Incomplete
    include_labour_day: ClassVar[bool]
    include_holy_thursday: ClassVar[bool]
    include_good_friday: ClassVar[bool]
    include_easter_saturday: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]
    immaculate_conception_label: ClassVar[str]
    def get_heroes_day(self, year): ...
    def get_founding_of_asuncion(self, year): ...
    def get_boqueron_battle_victory_day(self, year): ...
    def get_fixed_holidays(self, year): ...
