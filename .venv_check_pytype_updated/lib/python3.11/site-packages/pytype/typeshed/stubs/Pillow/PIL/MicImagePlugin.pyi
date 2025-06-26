from typing import Any, ClassVar, Literal
from typing_extensions import TypeAlias

from .TiffImagePlugin import TiffImageFile

_OleFileIO: TypeAlias = Any  # olefile.OleFileIO
_OleStream: TypeAlias = Any  # olefile.OleStream

class MicImageFile(TiffImageFile):
    ole: _OleFileIO
    format: ClassVar[Literal["MIC"]]
    format_description: ClassVar[str]
    fp: _OleStream
    frame: int | None
    images: list[list[str]]
    is_animated: bool
    def seek(self, frame: int) -> None: ...
    def tell(self) -> int | None: ...  # type: ignore[override]
