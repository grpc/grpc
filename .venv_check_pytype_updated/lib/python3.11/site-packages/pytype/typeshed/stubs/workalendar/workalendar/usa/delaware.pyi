from typing import ClassVar

from .core import UnitedStates

class Delaware(UnitedStates):
    include_good_friday: ClassVar[bool]
    include_thanksgiving_friday: ClassVar[bool]
    include_federal_presidents_day: ClassVar[bool]
    include_columbus_day: ClassVar[bool]
    include_election_day_even: ClassVar[bool]
