from typing import Final

from Xlib._typing import Unused
from Xlib.display import Display
from Xlib.protocol import rq
from Xlib.xobject import resource

extname: Final = "Generic Event Extension"
GenericEventCode: Final = 35

class GEQueryVersion(rq.ReplyRequest): ...

def query_version(self: Display | resource.Resource) -> GEQueryVersion: ...

class GenericEvent(rq.Event): ...

def add_event_data(self: Display | resource.Resource, extension: int, evtype: int, estruct: int) -> None: ...
def init(disp: Display, info: Unused) -> None: ...
