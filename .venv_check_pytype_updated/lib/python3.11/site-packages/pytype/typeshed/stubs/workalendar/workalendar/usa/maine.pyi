from typing import ClassVar

from .core import UnitedStates

class Maine(UnitedStates):
    include_thanksgiving_friday: ClassVar[bool]
    include_patriots_day: ClassVar[bool]
