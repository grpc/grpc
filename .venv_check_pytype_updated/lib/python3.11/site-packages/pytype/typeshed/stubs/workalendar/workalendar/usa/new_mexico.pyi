from typing import ClassVar

from .core import UnitedStates

class NewMexico(UnitedStates):
    include_thanksgiving_friday: ClassVar[bool]
    thanksgiving_friday_label: ClassVar[str]
    include_federal_presidents_day: ClassVar[bool]
