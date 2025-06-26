from _typeshed import Incomplete
from typing import ClassVar, Literal

from .ImageFile import ImageFile

xbm_head: Incomplete

class XbmImageFile(ImageFile):
    format: ClassVar[Literal["XBM"]]
    format_description: ClassVar[str]
