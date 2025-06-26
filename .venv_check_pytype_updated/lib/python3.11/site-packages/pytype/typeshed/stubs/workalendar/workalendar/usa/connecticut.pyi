from typing import ClassVar

from .core import UnitedStates

class Connecticut(UnitedStates):
    include_good_friday: ClassVar[bool]
    include_lincoln_birthday: ClassVar[bool]
