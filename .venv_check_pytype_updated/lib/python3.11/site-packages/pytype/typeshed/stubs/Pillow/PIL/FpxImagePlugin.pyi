from _typeshed import Incomplete
from typing import Any, ClassVar, Literal
from typing_extensions import TypeAlias

from ._imaging import _PixelAccessor
from .ImageFile import ImageFile

_OleFileIO: TypeAlias = Any  # olefile.OleFileIO
_OleStream: TypeAlias = Any  # olefile.OleStream

MODES: dict[tuple[int, ...], tuple[str, str]]

class FpxImageFile(ImageFile):
    ole: _OleFileIO
    format: ClassVar[Literal["FPX"]]
    format_description: ClassVar[str]
    fp: _OleStream | None
    maxid: int
    rawmode: str
    jpeg: dict[int, Incomplete]
    tile_prefix: Incomplete
    stream: list[str]
    def load(self) -> _PixelAccessor: ...
