from _typeshed import Incomplete
from typing import Final, Literal, NamedTuple
from typing_extensions import TypeAlias

_TagType: TypeAlias = Literal[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 16]
_TagTuple: TypeAlias = tuple[str, _TagType, int] | tuple[str, _TagInfo, int, dict[str, int]]

class _TagInfo(NamedTuple):
    value: Incomplete
    name: str
    type: _TagType
    length: int
    enum: dict[str, int]

class TagInfo(_TagInfo):
    def __new__(
        cls,
        value: Incomplete | None = None,
        name: str = "unknown",
        type: _TagType | None = None,
        length: int | None = None,
        enum: dict[str, int] | None = None,
    ): ...
    def cvt_enum(self, value): ...

def lookup(tag: int, group: int | None = None) -> _TagInfo: ...

BYTE: Final = 1
ASCII: Final = 2
SHORT: Final = 3
LONG: Final = 4
RATIONAL: Final = 5
SIGNED_BYTE: Final = 6
UNDEFINED: Final = 7
SIGNED_SHORT: Final = 8
SIGNED_LONG: Final = 9
SIGNED_RATIONAL: Final = 10
FLOAT: Final = 11
DOUBLE: Final = 12
IFD: Final = 13
LONG8: Final = 16
TAGS_V2: dict[int, _TagTuple]
TAGS_V2_GROUPS: dict[int, dict[int, _TagTuple]]
TAGS: dict[int, str]
TYPES: dict[int, str]
LIBTIFF_CORE: set[int]
