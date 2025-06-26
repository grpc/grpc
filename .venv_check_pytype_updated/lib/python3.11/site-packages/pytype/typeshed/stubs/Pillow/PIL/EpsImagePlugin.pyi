import sys
from _typeshed import Incomplete
from typing import ClassVar, Literal

from ._imaging import _PixelAccessor
from .ImageFile import ImageFile

split: Incomplete
field: Incomplete
if sys.platform == "win32":
    gs_binary: Literal["gswin32c", "gswin64c", "gs", False] | None
    gs_windows_binary: Literal["gswin32c", "gswin64c", "gs", False] | None
else:
    gs_binary: Literal["gs", False] | None
    gs_windows_binary: None

def has_ghostscript(): ...
def Ghostscript(tile, size, fp, scale: int = 1, transparency: bool = False): ...

class PSFile:
    fp: Incomplete
    char: Incomplete
    def __init__(self, fp) -> None: ...
    def seek(self, offset, whence=0) -> None: ...
    def readline(self): ...

class EpsImageFile(ImageFile):
    format: ClassVar[Literal["EPS"]]
    format_description: ClassVar[str]
    mode_map: Incomplete
    im: Incomplete
    tile: Incomplete
    def load(self, scale: int = 1, transparency: bool = False) -> _PixelAccessor: ...
    def load_seek(self, *args, **kwargs) -> None: ...
