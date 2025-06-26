from typing import ClassVar

from .core import UnitedStates

class Mississippi(UnitedStates):
    include_thanksgiving_friday: ClassVar[bool]
    include_confederation_day: ClassVar[bool]
    include_columbus_day: ClassVar[bool]
    martin_luther_king_label: ClassVar[str]
    veterans_day_label: ClassVar[str]
    national_memorial_day_label: ClassVar[str]
