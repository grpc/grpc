from typing import ClassVar

from .core import UnitedStates

class Virginia(UnitedStates):
    include_christmas_eve: ClassVar[bool]
    include_thanksgiving_friday: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    presidents_day_label: ClassVar[str]
    include_thanksgiving_wednesday: ClassVar[bool]
    def get_variable_days(self, year): ...
