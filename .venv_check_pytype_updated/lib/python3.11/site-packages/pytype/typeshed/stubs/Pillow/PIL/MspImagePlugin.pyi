from typing import ClassVar, Literal

from .ImageFile import ImageFile, PyDecoder

class MspImageFile(ImageFile):
    format: ClassVar[Literal["MSP"]]
    format_description: ClassVar[str]

class MspDecoder(PyDecoder):
    def decode(self, buffer): ...
