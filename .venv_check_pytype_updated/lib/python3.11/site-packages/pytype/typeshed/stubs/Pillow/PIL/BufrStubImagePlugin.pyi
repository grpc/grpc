from typing import ClassVar, Literal

from .ImageFile import StubImageFile

def register_handler(handler) -> None: ...

class BufrStubImageFile(StubImageFile):
    format: ClassVar[Literal["BUFR"]]
    format_description: ClassVar[str]
