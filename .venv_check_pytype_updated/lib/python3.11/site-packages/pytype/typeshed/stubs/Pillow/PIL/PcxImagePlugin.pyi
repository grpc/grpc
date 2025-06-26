from _typeshed import Incomplete
from typing import ClassVar, Literal

from .ImageFile import ImageFile

class PcxImageFile(ImageFile):
    format: ClassVar[Literal["PCX", "DCX"]]
    format_description: ClassVar[str]

SAVE: Incomplete
