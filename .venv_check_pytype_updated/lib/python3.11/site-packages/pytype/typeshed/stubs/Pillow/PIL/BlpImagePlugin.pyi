from _typeshed import Incomplete
from enum import IntEnum
from typing import ClassVar, Literal

from .ImageFile import ImageFile, PyDecoder, PyEncoder

class Format(IntEnum):
    JPEG: int

class Encoding(IntEnum):
    UNCOMPRESSED: int
    DXT: int
    UNCOMPRESSED_RAW_BGRA: int

class AlphaEncoding(IntEnum):
    DXT1: int
    DXT3: int
    DXT5: int

def unpack_565(i): ...
def decode_dxt1(data, alpha: bool = False): ...
def decode_dxt3(data): ...
def decode_dxt5(data): ...

class BLPFormatError(NotImplementedError): ...

class BlpImageFile(ImageFile):
    format: ClassVar[Literal["BLP"]]
    format_description: ClassVar[str]

class _BLPBaseDecoder(PyDecoder):
    magic: Incomplete
    def decode(self, buffer): ...

class BLP1Decoder(_BLPBaseDecoder): ...
class BLP2Decoder(_BLPBaseDecoder): ...

class BLPEncoder(PyEncoder):
    def encode(self, bufsize): ...
