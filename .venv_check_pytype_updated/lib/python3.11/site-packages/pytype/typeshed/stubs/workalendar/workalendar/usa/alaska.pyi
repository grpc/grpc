from _typeshed import Incomplete
from typing import ClassVar

from .core import UnitedStates

class Alaska(UnitedStates):
    FIXED_HOLIDAYS: Incomplete
    include_columbus_day: ClassVar[bool]
    def get_variable_days(self, year): ...
