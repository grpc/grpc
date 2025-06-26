from typing import ClassVar

from ..core import WesternCalendar

class Denmark(WesternCalendar):
    include_holy_thursday: ClassVar[bool]
    include_good_friday: ClassVar[bool]
    include_easter_sunday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_ascension: ClassVar[bool]
    include_whit_sunday: ClassVar[bool]
    whit_sunday_label: ClassVar[str]
    include_whit_monday: ClassVar[bool]
    whit_monday_label: ClassVar[str]
    include_boxing_day: ClassVar[bool]
    boxing_day_label: ClassVar[str]
    include_christmas_eve: ClassVar[bool]
    def get_store_bededag(self, year): ...
    def get_variable_days(self, year): ...
