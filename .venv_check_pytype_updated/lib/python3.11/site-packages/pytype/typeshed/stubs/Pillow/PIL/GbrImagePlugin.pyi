from _typeshed import Incomplete
from typing import ClassVar, Literal

from ._imaging import _PixelAccessor
from .ImageFile import ImageFile

class GbrImageFile(ImageFile):
    format: ClassVar[Literal["GBR"]]
    format_description: ClassVar[str]
    im: Incomplete
    def load(self) -> _PixelAccessor: ...
