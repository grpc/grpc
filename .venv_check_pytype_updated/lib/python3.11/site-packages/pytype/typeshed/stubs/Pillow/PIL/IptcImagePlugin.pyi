from collections.abc import Iterable
from typing import ClassVar, Final, Literal
from typing_extensions import deprecated

from ._imaging import _PixelAccessor
from .ImageFile import ImageFile

COMPRESSION: dict[int, str]
PAD: Final[bytes]

@deprecated("Deprecated since 10.2.0")
def i(c: bytes) -> int: ...
@deprecated("Deprecated since 10.2.0")
def dump(c: Iterable[int | bytes]) -> None: ...

class IptcImageFile(ImageFile):
    format: ClassVar[Literal["IPTC"]]
    format_description: ClassVar[str]
    def getint(self, key: tuple[int, int]) -> int: ...
    def field(self) -> tuple[tuple[int, int] | None, int]: ...
    def load(self) -> _PixelAccessor: ...

def getiptcinfo(im): ...
