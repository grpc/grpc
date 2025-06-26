from typing import ClassVar

from .core import UnitedStates

class Michigan(UnitedStates):
    include_christmas_eve: ClassVar[bool]
    include_thanksgiving_friday: ClassVar[bool]
    include_election_day_even: ClassVar[bool]
    include_columbus_day: ClassVar[bool]
    def get_fixed_holidays(self, year): ...
