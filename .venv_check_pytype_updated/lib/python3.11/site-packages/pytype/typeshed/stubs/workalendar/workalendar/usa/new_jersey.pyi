from typing import ClassVar

from .core import UnitedStates

class NewJersey(UnitedStates):
    include_good_friday: ClassVar[bool]
    include_election_day_every_year: ClassVar[bool]
