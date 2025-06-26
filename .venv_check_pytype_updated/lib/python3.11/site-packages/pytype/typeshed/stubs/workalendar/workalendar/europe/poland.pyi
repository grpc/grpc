from _typeshed import Incomplete
from typing import ClassVar

from ..core import WesternCalendar

class Poland(WesternCalendar):
    include_labour_day: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    include_easter_sunday: ClassVar[bool]
    include_easter_monday: ClassVar[bool]
    include_whit_sunday: ClassVar[bool]
    whit_sunday_label: ClassVar[str]
    include_corpus_christi: ClassVar[bool]
    include_assumption: ClassVar[bool]
    include_all_saints: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
