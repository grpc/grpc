from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Argentina(WesternCalendar):
    include_labour_day: ClassVar[bool]
    labour_day_label: ClassVar[str]
    include_fat_tuesday: ClassVar[bool]
    fat_tuesday_label: ClassVar[str]
    include_good_friday: ClassVar[bool]
    include_easter_saturday: ClassVar[bool]
    include_easter_sunday: ClassVar[bool]
    include_christmas: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]
    immaculate_conception_label: ClassVar[str]
    FIXED_HOLIDAYS: Incomplete
    def get_general_guemes_day(self, year): ...
    def get_general_martin_day(self, year): ...
    def get_soberania_day(self, year): ...
    def get_diversidad_day(self, year): ...
    def get_malvinas_day(self, year): ...
    def get_variable_days(self, year): ...
