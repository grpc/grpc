from typing import ClassVar, Literal

from .ImageFile import StubImageFile

def register_handler(handler) -> None: ...

class HDF5StubImageFile(StubImageFile):
    format: ClassVar[Literal["HDF5"]]
    format_description: ClassVar[str]
