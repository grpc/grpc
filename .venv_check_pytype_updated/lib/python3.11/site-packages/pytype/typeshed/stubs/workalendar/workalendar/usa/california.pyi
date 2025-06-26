from _typeshed import Incomplete
from typing import ClassVar

from .core import UnitedStates

class California(UnitedStates):
    include_thanksgiving_friday: ClassVar[bool]
    include_cesar_chavez_day: ClassVar[bool]
    include_columbus_day: ClassVar[bool]
    shift_exceptions: Incomplete
    def get_cesar_chavez_days(self, year): ...

class CaliforniaEducation(California):
    def get_variable_days(self, year): ...

class CaliforniaBerkeley(California):
    FIXED_HOLIDAYS: Incomplete
    include_cesar_chavez_day: ClassVar[bool]
    include_lincoln_birthday: ClassVar[bool]
    include_columbus_day: ClassVar[bool]
    columbus_day_label: ClassVar[str]

class CaliforniaSanFrancisco(California):
    include_cesar_chavez_day: ClassVar[bool]
    include_columbus_day: ClassVar[bool]

class CaliforniaWestHollywood(California):
    FIXED_HOLIDAYS: Incomplete
    include_cesar_chavez_day: ClassVar[bool]
    include_thanksgiving_friday: ClassVar[bool]
