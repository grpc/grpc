from _typeshed import Incomplete
from typing import ClassVar, Literal

from .ImageFile import ImageFile

MODES: Incomplete

class TgaImageFile(ImageFile):
    format: ClassVar[Literal["TGA"]]
    format_description: ClassVar[str]

SAVE: Incomplete
