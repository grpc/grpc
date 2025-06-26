from typing import ClassVar, Literal
from typing_extensions import TypeAlias

from ._imaging import _PixelAccessor
from .ImageFile import ImageFile

SUPPORTED: bool
_XMP_Tags: TypeAlias = dict[str, str | _XMP_Tags]

class WebPImageFile(ImageFile):
    format: ClassVar[Literal["WEBP"]]
    format_description: ClassVar[str]

    def getxmp(self) -> _XMP_Tags: ...
    def seek(self, frame: int) -> None: ...
    def load(self) -> _PixelAccessor: ...
    def load_seek(self, pos: int) -> None: ...
    def tell(self) -> int: ...
