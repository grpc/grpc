from _typeshed import Incomplete
from typing import ClassVar, Literal

from ._imaging import _PixelAccessor
from .ImageFile import ImageFile

class BoxReader:
    fp: Incomplete
    has_length: Incomplete
    length: Incomplete
    remaining_in_box: int
    def __init__(self, fp, length: int = -1) -> None: ...
    def read_fields(self, field_format): ...
    def read_boxes(self): ...
    def has_next_box(self): ...
    def next_box_type(self): ...

class Jpeg2KImageFile(ImageFile):
    format: ClassVar[Literal["JPEG2000"]]
    format_description: ClassVar[str]
    reduce: Incomplete
    tile: Incomplete
    def load(self) -> _PixelAccessor: ...
