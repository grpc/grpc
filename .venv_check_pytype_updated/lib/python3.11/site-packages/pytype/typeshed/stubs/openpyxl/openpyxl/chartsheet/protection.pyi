from _typeshed import ConvertibleToInt, Incomplete
from typing import ClassVar, Literal

from openpyxl.descriptors.base import Bool, Integer, String, _ConvertibleToBool
from openpyxl.descriptors.serialisable import Serialisable
from openpyxl.worksheet.protection import _Protected

class ChartsheetProtection(Serialisable, _Protected):
    tagname: ClassVar[str]
    algorithmName: String[Literal[True]]
    hashValue: Incomplete
    saltValue: Incomplete
    spinCount: Integer[Literal[True]]
    content: Bool[Literal[True]]
    objects: Bool[Literal[True]]
    __attrs__: ClassVar[tuple[str, ...]]
    password: Incomplete
    def __init__(
        self,
        content: _ConvertibleToBool | None = None,
        objects: _ConvertibleToBool | None = None,
        hashValue: Incomplete | None = None,
        spinCount: ConvertibleToInt | None = None,
        saltValue: Incomplete | None = None,
        algorithmName: str | None = None,
        password: Incomplete | None = None,
    ) -> None: ...
