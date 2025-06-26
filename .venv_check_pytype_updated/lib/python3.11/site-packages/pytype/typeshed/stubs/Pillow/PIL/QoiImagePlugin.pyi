from typing import ClassVar

from . import Image as Image, ImageFile as ImageFile
from ._binary import o8 as o8

class QoiImageFile(ImageFile.ImageFile):
    format: ClassVar[str]
    format_description: ClassVar[str]

class QoiDecoder(ImageFile.PyDecoder):
    def decode(self, buffer): ...
