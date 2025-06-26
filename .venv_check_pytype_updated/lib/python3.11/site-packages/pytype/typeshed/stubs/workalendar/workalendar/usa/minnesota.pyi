from typing import ClassVar

from .core import UnitedStates

class Minnesota(UnitedStates):
    include_thanksgiving_friday: ClassVar[bool]
    include_columbus_day: ClassVar[bool]
