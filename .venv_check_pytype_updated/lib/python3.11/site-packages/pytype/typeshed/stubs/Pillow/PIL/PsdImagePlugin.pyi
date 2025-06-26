from _typeshed import Incomplete
from typing import ClassVar, Literal

from .ImageFile import ImageFile

MODES: Incomplete

class PsdImageFile(ImageFile):
    format: ClassVar[Literal["PSD"]]
    format_description: ClassVar[str]
    tile: Incomplete
    frame: Incomplete
    fp: Incomplete
    def seek(self, layer): ...
    def tell(self): ...
    im: Incomplete
    def load_prepare(self) -> None: ...
