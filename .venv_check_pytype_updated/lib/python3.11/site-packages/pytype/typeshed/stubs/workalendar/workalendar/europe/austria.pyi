from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Austria(WesternCalendar):
    FIXED_HOLIDAYS: Incomplete
    include_labour_day: ClassVar[bool]
    labour_day_label: ClassVar[str]
    include_epiphany: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_ascension: ClassVar[bool]
    include_whit_monday: ClassVar[bool]
    include_corpus_christi: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_immaculate_conception: ClassVar[bool]
    include_christmas: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    boxing_day_label: ClassVar[str]
