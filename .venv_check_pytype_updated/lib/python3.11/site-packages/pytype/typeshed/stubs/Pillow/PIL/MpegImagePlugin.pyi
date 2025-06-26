from _typeshed import Incomplete
from typing import ClassVar, Literal

from .ImageFile import ImageFile

class BitStream:
    fp: Incomplete
    bits: int
    bitbuffer: int
    def __init__(self, fp) -> None: ...
    def next(self): ...
    def peek(self, bits): ...
    def skip(self, bits) -> None: ...
    def read(self, bits): ...

class MpegImageFile(ImageFile):
    format: ClassVar[Literal["MPEG"]]
    format_description: ClassVar[str]
