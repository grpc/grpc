from typing import ClassVar, Literal

from .ImageFile import ImageFile

class McIdasImageFile(ImageFile):
    format: ClassVar[Literal["MCIDAS"]]
    format_description: ClassVar[str]
