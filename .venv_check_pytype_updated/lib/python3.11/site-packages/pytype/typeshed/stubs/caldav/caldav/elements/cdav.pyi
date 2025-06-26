import datetime
from typing import ClassVar

from .base import BaseElement, NamedBaseElement, ValuedBaseElement

class CalendarQuery(BaseElement):
    tag: ClassVar[str]

class FreeBusyQuery(BaseElement):
    tag: ClassVar[str]

class Mkcalendar(BaseElement):
    tag: ClassVar[str]

class CalendarMultiGet(BaseElement):
    tag: ClassVar[str]

class ScheduleInboxURL(BaseElement):
    tag: ClassVar[str]

class ScheduleOutboxURL(BaseElement):
    tag: ClassVar[str]

class Filter(BaseElement):
    tag: ClassVar[str]

class CompFilter(NamedBaseElement):
    tag: ClassVar[str]

class PropFilter(NamedBaseElement):
    tag: ClassVar[str]

class ParamFilter(NamedBaseElement):
    tag: ClassVar[str]

class TextMatch(ValuedBaseElement):
    tag: ClassVar[str]
    def __init__(self, value, collation: str = "i;octet", negate: bool = False) -> None: ...

class TimeRange(BaseElement):
    tag: ClassVar[str]
    def __init__(self, start: datetime.datetime | None = None, end: datetime.datetime | None = None) -> None: ...

class NotDefined(BaseElement):
    tag: ClassVar[str]

class CalendarData(BaseElement):
    tag: ClassVar[str]

class Expand(BaseElement):
    tag: ClassVar[str]
    def __init__(self, start: datetime.datetime | None, end: datetime.datetime | None = None) -> None: ...

class Comp(NamedBaseElement):
    tag: ClassVar[str]

class CalendarUserAddressSet(BaseElement):
    tag: ClassVar[str]

class CalendarUserType(BaseElement):
    tag: ClassVar[str]

class CalendarHomeSet(BaseElement):
    tag: ClassVar[str]

class Calendar(BaseElement):
    tag: ClassVar[str]

class CalendarDescription(ValuedBaseElement):
    tag: ClassVar[str]

class CalendarTimeZone(ValuedBaseElement):
    tag: ClassVar[str]

class SupportedCalendarComponentSet(ValuedBaseElement):
    tag: ClassVar[str]

class SupportedCalendarData(ValuedBaseElement):
    tag: ClassVar[str]

class MaxResourceSize(ValuedBaseElement):
    tag: ClassVar[str]

class MinDateTime(ValuedBaseElement):
    tag: ClassVar[str]

class MaxDateTime(ValuedBaseElement):
    tag: ClassVar[str]

class MaxInstances(ValuedBaseElement):
    tag: ClassVar[str]

class MaxAttendeesPerInstance(ValuedBaseElement):
    tag: ClassVar[str]

class Allprop(BaseElement):
    tag: ClassVar[str]

class ScheduleTag(BaseElement):
    tag: ClassVar[str]
