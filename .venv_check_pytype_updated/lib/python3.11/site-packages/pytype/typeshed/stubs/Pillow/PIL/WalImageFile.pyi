from typing import ClassVar, Literal

from . import ImageFile
from ._imaging import _PixelAccessor

class WalImageFile(ImageFile.ImageFile):
    format: ClassVar[Literal["WAL"]]
    format_description: ClassVar[str]
    def load(self) -> _PixelAccessor: ...

def open(filename): ...

quake2palette: bytes
