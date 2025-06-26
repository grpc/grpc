from typing import ClassVar

from .core import UnitedStates

class TexasBase(UnitedStates):
    include_columbus_day: ClassVar[bool]
    texas_include_confederate_heroes: ClassVar[bool]
    texas_include_independance_day: ClassVar[bool]
    texas_san_jacinto_day: ClassVar[bool]
    texas_emancipation_day: ClassVar[bool]
    texas_lyndon_johnson_day: ClassVar[bool]
    include_thanksgiving_friday: ClassVar[bool]
    include_christmas_eve: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
    def get_fixed_holidays(self, year): ...

class Texas(TexasBase):
    texas_include_confederate_heroes: ClassVar[bool]
    texas_include_independance_day: ClassVar[bool]
    texas_san_jacinto_day: ClassVar[bool]
    texas_emancipation_day: ClassVar[bool]
    texas_lyndon_johnson_day: ClassVar[bool]
    include_thanksgiving_friday: ClassVar[bool]
    include_christmas_eve: ClassVar[bool]
    include_boxing_day: ClassVar[bool]
