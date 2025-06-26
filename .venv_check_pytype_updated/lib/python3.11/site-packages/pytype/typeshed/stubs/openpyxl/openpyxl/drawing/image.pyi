from _typeshed import SupportsRead
from pathlib import Path
from types import ModuleType
from typing import Any, Literal
from typing_extensions import TypeAlias

# Is actually PIL.Image.Image
_PILImageImage: TypeAlias = Any
# same as first parameter of PIL.Image.open
_PILImageFilePath: TypeAlias = str | bytes | Path | SupportsRead[bytes]

PILImage: ModuleType | Literal[False]

class Image:
    anchor: str
    ref: _PILImageImage | _PILImageFilePath
    format: str
    def __init__(self, img: _PILImageImage | _PILImageFilePath) -> None: ...
    @property
    def path(self) -> str: ...
