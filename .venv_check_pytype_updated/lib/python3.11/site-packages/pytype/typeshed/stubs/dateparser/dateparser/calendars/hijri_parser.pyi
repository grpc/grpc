from _typeshed import Incomplete
from typing import Any

from dateparser.calendars import non_gregorian_parser

class hijri:
    @classmethod
    def to_gregorian(cls, year: Incomplete | None = ..., month: Incomplete | None = ..., day: Incomplete | None = ...): ...
    @classmethod
    def from_gregorian(cls, year: Incomplete | None = ..., month: Incomplete | None = ..., day: Incomplete | None = ...): ...
    @classmethod
    def month_length(cls, year, month): ...

class HijriDate:
    year: Any
    month: Any
    day: Any
    def __init__(self, year, month, day) -> None: ...
    def weekday(self): ...

class hijri_parser(non_gregorian_parser):
    calendar_converter: Any
    default_year: int
    default_month: int
    default_day: int
    non_gregorian_date_cls: Any
