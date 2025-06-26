from typing import ClassVar

from .core import UnitedStates

class Indiana(UnitedStates):
    include_good_friday: ClassVar[bool]
    include_thanksgiving_friday: ClassVar[bool]
    thanksgiving_friday_label: ClassVar[str]
    include_federal_presidents_day: ClassVar[bool]
    label_washington_birthday_december: ClassVar[str]
    include_election_day_even: ClassVar[bool]
    election_day_label: ClassVar[str]
    def get_washington_birthday_december(self, year): ...
    def get_primary_election_day(self, year): ...
    def get_variable_days(self, year): ...
