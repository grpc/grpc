from typing import ClassVar, Literal

from .BmpImagePlugin import BmpImageFile

class CurImageFile(BmpImageFile):
    format: ClassVar[Literal["CUR"]]
