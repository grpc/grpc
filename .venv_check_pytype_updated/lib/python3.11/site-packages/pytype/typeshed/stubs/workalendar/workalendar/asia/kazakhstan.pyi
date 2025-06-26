from _typeshed import Incomplete
from typing import ClassVar

from ..core import IslamicCalendar, OrthodoxCalendar

class Kazakhstan(OrthodoxCalendar, IslamicCalendar):
    include_christmas: ClassVar[bool]
    include_christmas_eve: ClassVar[bool]
    include_new_years_day: ClassVar[bool]
    include_orthodox_christmas: ClassVar[bool]
    include_epiphany: ClassVar[bool]
    include_good_friday: ClassVar[bool]
    include_easter_saturday: ClassVar[bool]
    include_easter_sunday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_prophet_birthday: ClassVar[bool]
    include_day_after_prophet_birthday: ClassVar[bool]
    include_start_ramadan: ClassVar[bool]
    include_eid_al_fitr: ClassVar[bool]
    length_eid_al_fitr: int
    include_eid_al_adha: ClassVar[bool]
    length_eid_al_adha: int
    include_day_of_sacrifice: ClassVar[bool]
    day_of_sacrifice_label: ClassVar[str]
    include_islamic_new_year: ClassVar[bool]
    include_laylat_al_qadr: ClassVar[bool]
    include_nuzul_al_quran: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    def get_fixed_holidays(self, year): ...
    def get_variable_days(self, year): ...
