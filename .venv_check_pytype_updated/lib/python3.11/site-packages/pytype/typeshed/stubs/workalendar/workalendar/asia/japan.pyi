from _typeshed import Incomplete

from ..core import Calendar

class Japan(Calendar):
    WEEKEND_DAYS: Incomplete
    FIXED_HOLIDAYS: Incomplete
    def get_fixed_holidays(self, year): ...
    def get_variable_days(self, year): ...

class JapanBank(Japan):
    FIXED_HOLIDAYS: Incomplete
