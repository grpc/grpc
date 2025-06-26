from typing import ClassVar

from .core import UnitedStates

class Louisiana(UnitedStates):
    include_good_friday: ClassVar[bool]
    include_election_day_even: ClassVar[bool]
    include_columbus_day: ClassVar[bool]
    include_fat_tuesday: ClassVar[bool]
