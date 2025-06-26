from _typeshed import Incomplete
from typing import ClassVar

from .core import UnitedStates

class Massachusetts(UnitedStates):
    include_patriots_day: ClassVar[bool]

class SuffolkCountyMassachusetts(Massachusetts):
    FIXED_HOLIDAYS: Incomplete
