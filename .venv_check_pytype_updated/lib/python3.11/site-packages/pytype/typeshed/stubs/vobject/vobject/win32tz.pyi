import datetime
import sys
from typing import Any

if sys.platform == "win32":
    handle: Any
    tzparent: Any
    parentsize: Any
    localkey: Any
    WEEKS: Any
    def list_timezones(): ...

    class win32tz(datetime.tzinfo):
        data: Any
        def __init__(self, name) -> None: ...
        def utcoffset(self, dt): ...
        def dst(self, dt): ...
        def tzname(self, dt): ...

    def pickNthWeekday(year, month, dayofweek, hour, minute, whichweek): ...

    class win32tz_data:
        display: Any
        dstname: Any
        stdname: Any
        stdoffset: Any
        dstoffset: Any
        stdmonth: Any
        stddayofweek: Any
        stdweeknumber: Any
        stdhour: Any
        stdminute: Any
        dstmonth: Any
        dstdayofweek: Any
        dstweeknumber: Any
        dsthour: Any
        dstminute: Any
        def __init__(self, path) -> None: ...

    def valuesToDict(key): ...
