from _typeshed import Incomplete
from typing import ClassVar

from .core import UnitedStates

class WestVirginia(UnitedStates):
    include_thanksgiving_friday: ClassVar[bool]
    include_election_day_even: ClassVar[bool]
    election_day_label: ClassVar[str]
    west_virginia_include_christmas_eve: ClassVar[bool]
    west_virginia_include_nye: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    shift_exceptions: Incomplete
    def get_fixed_holidays(self, year): ...
