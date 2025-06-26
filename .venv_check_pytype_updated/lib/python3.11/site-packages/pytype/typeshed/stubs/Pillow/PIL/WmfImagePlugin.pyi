import sys
from _typeshed import Incomplete
from typing import ClassVar, Literal

from ._imaging import _PixelAccessor
from .ImageFile import StubImageFile

def register_handler(handler) -> None: ...

if sys.platform == "win32":
    class WmfHandler:
        bbox: Incomplete
        def open(self, im) -> None: ...
        def load(self, im): ...

class WmfStubImageFile(StubImageFile):
    format: ClassVar[Literal["WMF"]]
    format_description: ClassVar[str]
    def load(self, dpi: Incomplete | None = None) -> _PixelAccessor: ...
