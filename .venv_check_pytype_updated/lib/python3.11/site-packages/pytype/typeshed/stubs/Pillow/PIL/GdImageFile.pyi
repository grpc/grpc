from typing import ClassVar, Literal

from .ImageFile import ImageFile

class GdImageFile(ImageFile):
    format: ClassVar[Literal["GD"]]
    format_description: ClassVar[str]

def open(fp, mode: str = "r"): ...
