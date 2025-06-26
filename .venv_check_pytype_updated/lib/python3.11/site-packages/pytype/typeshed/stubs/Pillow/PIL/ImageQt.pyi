from _typeshed import Incomplete
from typing import Any, Literal
from typing_extensions import TypeAlias

from .Image import Image

# imported from either of {PyQt6,PySide6,PyQt5,PySide2}.QtGui
# These are way too complex, with 4 different possible sources (2 deprecated)
# And we don't want to force the user to install PyQt or Pyside when they may not even use it.
_QImage: TypeAlias = Any
_QPixmap: TypeAlias = Any

qt_versions: Incomplete
qt_is_installed: bool
qt_version: Incomplete

def rgb(r: int, g: int, b: int, a: int = 255) -> int: ...
def fromqimage(im: ImageQt | _QImage) -> Image: ...
def fromqpixmap(im: ImageQt | _QImage) -> Image: ...
def align8to32(bytes: bytes, width: int, mode: Literal["1", "L", "P"]) -> bytes: ...

class ImageQt(_QImage):
    def __init__(self, im: Image) -> None: ...

def toqimage(im: Image) -> ImageQt: ...
def toqpixmap(im: Image) -> _QPixmap: ...
