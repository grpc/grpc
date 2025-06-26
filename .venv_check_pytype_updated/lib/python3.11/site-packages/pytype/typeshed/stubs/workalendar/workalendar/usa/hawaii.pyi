from _typeshed import Incomplete
from typing import ClassVar

from .core import UnitedStates

class Hawaii(UnitedStates):
    include_good_friday: ClassVar[bool]
    include_columbus_day: ClassVar[bool]
    include_election_day_even: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
    def get_statehood_day(self, year): ...
    def get_variable_days(self, year): ...
