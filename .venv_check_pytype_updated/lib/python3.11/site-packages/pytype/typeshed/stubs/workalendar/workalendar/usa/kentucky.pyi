from _typeshed import Incomplete
from typing import ClassVar

from .core import UnitedStates

class Kentucky(UnitedStates):
    include_good_friday: ClassVar[bool]
    include_thanksgiving_friday: ClassVar[bool]
    include_christmas_eve: ClassVar[bool]
    include_columbus_day: ClassVar[bool]
    include_federal_presidents_day: ClassVar[bool]
    FIXED_HOLIDAYS: Incomplete
