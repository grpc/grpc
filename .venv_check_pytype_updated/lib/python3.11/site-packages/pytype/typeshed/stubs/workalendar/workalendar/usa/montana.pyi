from typing import ClassVar

from .core import UnitedStates

class Montana(UnitedStates):
    include_election_day_even: ClassVar[bool]
    def get_variable_days(self, year): ...
