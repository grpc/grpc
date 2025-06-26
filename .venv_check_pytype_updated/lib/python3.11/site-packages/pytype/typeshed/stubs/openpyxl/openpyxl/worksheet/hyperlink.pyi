from _typeshed import Incomplete
from typing import ClassVar, Literal

from openpyxl.descriptors.base import String
from openpyxl.descriptors.serialisable import Serialisable

class Hyperlink(Serialisable):
    tagname: ClassVar[str]
    ref: String[Literal[False]]
    location: String[Literal[True]]
    tooltip: String[Literal[True]]
    display: String[Literal[True]]
    id: Incomplete
    target: String[Literal[True]]
    __attrs__: ClassVar[tuple[str, ...]]
    def __init__(
        self,
        ref: str,
        location: str | None = None,
        tooltip: str | None = None,
        display: str | None = None,
        id: Incomplete | None = None,
        target: str | None = None,
    ) -> None: ...

class HyperlinkList(Serialisable):
    tagname: ClassVar[str]
    hyperlink: Incomplete
    def __init__(self, hyperlink=()) -> None: ...
    def __bool__(self) -> bool: ...
    def __len__(self) -> int: ...
    def append(self, value) -> None: ...
