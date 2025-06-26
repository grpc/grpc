from typing import ClassVar

from .core import UnitedStates

class NorthCarolina(UnitedStates):
    include_good_friday: ClassVar[bool]
    include_christmas_eve: ClassVar[bool]
    include_thanksgiving_friday: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    include_federal_presidents_day: ClassVar[bool]
    include_columbus_day: ClassVar[bool]
    def get_christmas_shifts(self, year): ...
    def get_variable_days(self, year): ...
