from _typeshed import Incomplete
from typing import ClassVar

from ..core import Calendar

class Israel(Calendar):
    include_new_years_day: ClassVar[bool]
    WEEKEND_DAYS: Incomplete
    def get_variable_days(self, year): ...
    def get_hebrew_independence_day(self, jewish_year): ...
