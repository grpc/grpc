from typing import ClassVar

from .core import UnitedStates

class Iowa(UnitedStates):
    include_thanksgiving_friday: ClassVar[bool]
    include_columbus_day: ClassVar[bool]
    include_federal_presidents_day: ClassVar[bool]
