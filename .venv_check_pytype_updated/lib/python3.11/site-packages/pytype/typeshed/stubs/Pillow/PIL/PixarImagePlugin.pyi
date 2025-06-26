from typing import ClassVar, Literal

from .ImageFile import ImageFile

class PixarImageFile(ImageFile):
    format: ClassVar[Literal["PIXAR"]]
    format_description: ClassVar[str]
