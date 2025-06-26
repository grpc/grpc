from _typeshed import Incomplete
from typing import ClassVar

from .core import UnitedStates

class Wisconsin(UnitedStates):
    include_columbus_day: ClassVar[bool]
    include_federal_presidents_day: ClassVar[bool]
    include_christmas_eve: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
