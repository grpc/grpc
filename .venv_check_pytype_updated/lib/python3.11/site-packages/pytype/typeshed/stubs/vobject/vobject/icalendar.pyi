from _typeshed import Incomplete
from datetime import timedelta
from typing import Any

from .base import Component
from .behavior import Behavior

DATENAMES: tuple[str, ...]
RULENAMES: tuple[str, ...]
DATESANDRULES: tuple[str, ...]
PRODID: str
WEEKDAYS: tuple[str, ...]
FREQUENCIES: tuple[str, ...]
zeroDelta: timedelta
twoHours: timedelta

def toUnicode(s: str | bytes) -> str: ...
def registerTzid(tzid, tzinfo) -> None: ...
def getTzid(tzid, smart: bool = True): ...

utc: Any  # dateutil.tz.tz.tzutc

class TimezoneComponent(Component):
    isNative: bool
    behavior: Any
    tzinfo: Any
    name: str
    useBegin: bool
    def __init__(self, tzinfo: Incomplete | None = None, *args, **kwds) -> None: ...
    @classmethod
    def registerTzinfo(cls, tzinfo): ...
    def gettzinfo(self): ...
    tzid: Any
    daylight: Any
    standard: Any
    def settzinfo(self, tzinfo, start: int = 2000, end: int = 2030): ...
    normal_attributes: Any
    @staticmethod
    def pickTzid(tzinfo, allowUTC: bool = False): ...
    def prettyPrint(self, level, tabwidth) -> None: ...  # type: ignore[override]

class RecurringComponent(Component):
    isNative: bool
    def __init__(self, *args, **kwds) -> None: ...
    def getrruleset(self, addRDate: bool = False): ...
    def setrruleset(self, rruleset): ...
    rruleset: Any
    def __setattr__(self, name, value) -> None: ...

class TextBehavior(Behavior):
    base64string: str
    @classmethod
    def decode(cls, line) -> None: ...
    @classmethod
    def encode(cls, line) -> None: ...

class VCalendarComponentBehavior(Behavior):
    defaultBehavior: Any
    isComponent: bool

class RecurringBehavior(VCalendarComponentBehavior):
    hasNative: bool
    @staticmethod
    def transformToNative(obj): ...
    @staticmethod
    def transformFromNative(obj): ...
    @staticmethod
    def generateImplicitParameters(obj) -> None: ...

class DateTimeBehavior(Behavior):
    hasNative: bool
    @staticmethod
    def transformToNative(obj): ...
    @classmethod
    def transformFromNative(cls, obj): ...

class UTCDateTimeBehavior(DateTimeBehavior):
    forceUTC: bool

class DateOrDateTimeBehavior(Behavior):
    hasNative: bool
    @staticmethod
    def transformToNative(obj): ...
    @staticmethod
    def transformFromNative(obj): ...

class MultiDateBehavior(Behavior):
    hasNative: bool
    @staticmethod
    def transformToNative(obj): ...
    @staticmethod
    def transformFromNative(obj): ...

class MultiTextBehavior(Behavior):
    listSeparator: str
    @classmethod
    def decode(cls, line) -> None: ...
    @classmethod
    def encode(cls, line) -> None: ...

class SemicolonMultiTextBehavior(MultiTextBehavior):
    listSeparator: str

class VCalendar2_0(VCalendarComponentBehavior):
    name: str
    description: str
    versionString: str
    sortFirst: Any
    knownChildren: Any
    @classmethod
    def generateImplicitParameters(cls, obj) -> None: ...
    @classmethod
    def serialize(cls, obj, buf, lineLength, validate: bool = True): ...

class VTimezone(VCalendarComponentBehavior):
    name: str
    hasNative: bool
    description: str
    sortFirst: Any
    knownChildren: Any
    @classmethod
    def validate(cls, obj, raiseException, *args): ...
    @staticmethod
    def transformToNative(obj): ...
    @staticmethod
    def transformFromNative(obj): ...

class TZID(Behavior): ...

class DaylightOrStandard(VCalendarComponentBehavior):
    hasNative: bool
    knownChildren: Any

class VEvent(RecurringBehavior):
    name: str
    sortFirst: Any
    description: str
    knownChildren: Any
    @classmethod
    def validate(cls, obj, raiseException, *args): ...

class VTodo(RecurringBehavior):
    name: str
    description: str
    knownChildren: Any
    @classmethod
    def validate(cls, obj, raiseException, *args): ...

class VJournal(RecurringBehavior):
    name: str
    knownChildren: Any

class VFreeBusy(VCalendarComponentBehavior):
    name: str
    description: str
    sortFirst: Any
    knownChildren: Any

class VAlarm(VCalendarComponentBehavior):
    name: str
    description: str
    knownChildren: Any
    @staticmethod
    def generateImplicitParameters(obj) -> None: ...
    @classmethod
    def validate(cls, obj, raiseException, *args): ...

class VAvailability(VCalendarComponentBehavior):
    name: str
    description: str
    sortFirst: Any
    knownChildren: Any
    @classmethod
    def validate(cls, obj, raiseException, *args): ...

class Available(RecurringBehavior):
    name: str
    sortFirst: Any
    description: str
    knownChildren: Any
    @classmethod
    def validate(cls, obj, raiseException, *args): ...

class Duration(Behavior):
    name: str
    hasNative: bool
    @staticmethod
    def transformToNative(obj): ...
    @staticmethod
    def transformFromNative(obj): ...

class Trigger(Behavior):
    name: str
    description: str
    hasNative: bool
    forceUTC: bool
    @staticmethod
    def transformToNative(obj): ...
    @staticmethod
    def transformFromNative(obj): ...

class PeriodBehavior(Behavior):
    hasNative: bool
    @staticmethod
    def transformToNative(obj): ...
    @classmethod
    def transformFromNative(cls, obj): ...

class FreeBusy(PeriodBehavior):
    name: str
    forceUTC: bool

class RRule(Behavior): ...

utcDateTimeList: Any
dateTimeOrDateList: Any
textList: Any

def numToDigits(num, places): ...
def timedeltaToString(delta): ...
def timeToString(dateOrDateTime): ...
def dateToString(date): ...
def dateTimeToString(dateTime, convertToUTC: bool = False): ...
def deltaToOffset(delta): ...
def periodToString(period, convertToUTC: bool = False): ...
def isDuration(s): ...
def stringToDate(s): ...
def stringToDateTime(s, tzinfo: Incomplete | None = None): ...

escapableCharList: str

def stringToTextValues(s, listSeparator: str = ",", charList: Incomplete | None = None, strict: bool = False): ...
def stringToDurations(s, strict: bool = False): ...
def parseDtstart(contentline, allowSignatureMismatch: bool = False): ...
def stringToPeriod(s, tzinfo: Incomplete | None = None): ...
def getTransition(transitionTo, year, tzinfo): ...
def tzinfo_eq(tzinfo1, tzinfo2, startYear: int = 2000, endYear: int = 2020): ...
