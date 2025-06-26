from _typeshed import Incomplete
from typing import ClassVar, Literal

from .ImageFile import ImageFile

COMMENT: str
DATE: str
EQUIPMENT: str
FRAMES: str
LUT: str
NAME: str
SCALE: str
SIZE: str
MODE: str
TAGS: Incomplete
OPEN: Incomplete
split: Incomplete

def number(s): ...

class ImImageFile(ImageFile):
    format: ClassVar[Literal["IM"]]
    format_description: ClassVar[str]
    @property
    def n_frames(self): ...
    @property
    def is_animated(self): ...
    frame: Incomplete
    fp: Incomplete
    tile: Incomplete
    def seek(self, frame) -> None: ...
    def tell(self): ...

SAVE: Incomplete
