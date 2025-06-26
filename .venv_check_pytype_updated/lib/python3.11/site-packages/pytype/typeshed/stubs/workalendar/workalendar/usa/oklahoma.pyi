from typing import ClassVar

from .core import UnitedStates

class Oklahoma(UnitedStates):
    include_thanksgiving_friday: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    include_columbus_day: ClassVar[bool]
