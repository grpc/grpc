from _typeshed import Incomplete
from typing import ClassVar, Literal

from .ImageFile import ImageFile

field: Incomplete

class ImtImageFile(ImageFile):
    format: ClassVar[Literal["IMT"]]
    format_description: ClassVar[str]
