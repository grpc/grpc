from _typeshed import Incomplete
from typing import ClassVar, Literal

from .ImageFile import ImageFile, PyDecoder

b_whitespace: bytes
MODES: Incomplete

class PpmImageFile(ImageFile):
    format: ClassVar[Literal["PPM"]]
    format_description: ClassVar[str]

class PpmPlainDecoder(PyDecoder):
    def decode(self, buffer): ...

class PpmDecoder(PyDecoder):
    def decode(self, buffer): ...
