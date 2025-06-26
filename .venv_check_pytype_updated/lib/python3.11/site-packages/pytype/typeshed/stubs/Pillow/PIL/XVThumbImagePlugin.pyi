from typing import ClassVar, Literal

from .ImageFile import ImageFile

PALETTE: bytes

class XVThumbImageFile(ImageFile):
    format: ClassVar[Literal["XVThumb"]]
    format_description: ClassVar[str]
