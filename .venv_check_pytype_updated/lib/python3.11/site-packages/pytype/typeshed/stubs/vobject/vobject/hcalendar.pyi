from _typeshed import Incomplete

from .icalendar import VCalendar2_0

class HCalendar(VCalendar2_0):
    name: str
    @classmethod
    def serialize(cls, obj, buf: Incomplete | None = None, lineLength: Incomplete | None = None, validate: bool = True): ...
