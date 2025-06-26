from typing import ClassVar, Literal

from .ImageFile import ImageFile

class FliImageFile(ImageFile):
    format: ClassVar[Literal["FLI"]]
    format_description: ClassVar[str]
    def seek(self, frame) -> None: ...
    def tell(self): ...
