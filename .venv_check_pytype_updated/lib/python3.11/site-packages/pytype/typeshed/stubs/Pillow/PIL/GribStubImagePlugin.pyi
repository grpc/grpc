from typing import ClassVar, Literal

from .ImageFile import StubImageFile

def register_handler(handler) -> None: ...

class GribStubImageFile(StubImageFile):
    format: ClassVar[Literal["GRIB"]]
    format_description: ClassVar[str]
