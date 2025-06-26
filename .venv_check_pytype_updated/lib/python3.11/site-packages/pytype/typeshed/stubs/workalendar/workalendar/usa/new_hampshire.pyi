from typing import ClassVar

from .core import UnitedStates

class NewHampshire(UnitedStates):
    include_thanksgiving_friday: ClassVar[bool]
    martin_luther_king_label: ClassVar[str]
