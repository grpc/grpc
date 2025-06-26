from typing import ClassVar

from .core import UnitedStates

class Kansas(UnitedStates):
    include_federal_presidents_day: ClassVar[bool]
    include_columbus_day: ClassVar[bool]
