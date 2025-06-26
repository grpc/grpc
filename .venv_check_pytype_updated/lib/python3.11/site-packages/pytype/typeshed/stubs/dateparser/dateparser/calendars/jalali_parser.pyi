from typing import Any

from dateparser.calendars import non_gregorian_parser

class PersianDate:
    year: Any
    month: Any
    day: Any
    def __init__(self, year, month, day) -> None: ...
    def weekday(self): ...

class jalali_parser(non_gregorian_parser):
    calendar_converter: Any
    default_year: int
    default_month: int
    default_day: int
    non_gregorian_date_cls: Any
