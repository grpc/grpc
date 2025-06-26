from typing import ClassVar

from . import Image as Image, ImageFile as ImageFile

class FitsImageFile(ImageFile.ImageFile):
    format: ClassVar[str]
    format_description: ClassVar[str]
