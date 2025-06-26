from _typeshed import Incomplete
from typing import ClassVar

from .core import UnitedStates

class SouthCarolina(UnitedStates):
    FIXED_HOLIDAYS: Incomplete
    include_thanksgiving_friday: ClassVar[bool]
    include_christmas_eve: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    include_columbus_day: ClassVar[bool]
