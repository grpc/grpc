from typing import ClassVar, Literal

from .ImageFile import ImageFile

class SunImageFile(ImageFile):
    format: ClassVar[Literal["SUN"]]
    format_description: ClassVar[str]
