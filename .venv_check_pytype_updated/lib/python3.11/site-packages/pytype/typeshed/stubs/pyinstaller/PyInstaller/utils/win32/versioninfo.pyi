from _typeshed import SliceableBuffer
from collections.abc import Sequence
from typing import Literal, Protocol
from typing_extensions import TypeAlias

_FourIntSequence: TypeAlias = Sequence[int]
_TwoIntSequence: TypeAlias = Sequence[int]

class _Kid(Protocol):
    def toRaw(self) -> bytes: ...
    def __str__(self, indent: str = "", /) -> str: ...

# Used by other types referenced in https://pyinstaller.org/en/stable/spec-files.html#spec-file-operation
class VSVersionInfo:
    ffi: FixedFileInfo | None
    kids: list[_Kid]
    def __init__(self, ffi: FixedFileInfo | None = None, kids: list[_Kid] | None = None) -> None: ...
    def fromRaw(self, data: SliceableBuffer) -> int: ...
    def toRaw(self) -> bytes: ...
    def __eq__(self, other: object) -> bool: ...
    def __str__(self, indent: str = "") -> str: ...

class FixedFileInfo:
    sig: Literal[0xFEEF04BD]
    strucVersion: Literal[0x10000]
    fileVersionMS: int
    fileVersionLS: int
    productVersionMS: int
    productVersionLS: int
    fileFlagsMask: int
    fileFlags: int
    fileOS: int
    fileType: int
    fileSubtype: int
    fileDateMS: int
    fileDateLS: int
    def __init__(
        self,
        filevers: _FourIntSequence = ...,
        prodvers: _FourIntSequence = ...,
        mask: int = 0x3F,
        flags: int = 0x0,
        OS: int = 0x40004,
        fileType: int = 0x1,
        subtype: int = 0x0,
        date: _TwoIntSequence = ...,
    ) -> None: ...
    def fromRaw(self, data: SliceableBuffer, i: int) -> int: ...
    def toRaw(self) -> bytes: ...
    def __eq__(self, other: object) -> bool: ...
    def __str__(self, indent: str = "") -> str: ...
