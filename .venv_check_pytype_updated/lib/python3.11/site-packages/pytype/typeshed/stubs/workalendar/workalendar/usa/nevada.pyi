from typing import ClassVar

from .core import UnitedStates

class Nevada(UnitedStates):
    include_thanksgiving_friday: ClassVar[bool]
    thanksgiving_friday_label: ClassVar[str]
    include_columbus_day: ClassVar[bool]
    def get_variable_days(self, year): ...
