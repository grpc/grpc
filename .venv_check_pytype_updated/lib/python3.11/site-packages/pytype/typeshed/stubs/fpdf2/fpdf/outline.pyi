from _typeshed import Incomplete
from collections.abc import Generator, Iterable
from typing import NamedTuple

from .structure_tree import StructElem
from .syntax import Destination, PDFObject, PDFString

class OutlineSection(NamedTuple):
    name: str
    level: str
    page_number: int
    dest: Destination
    struct_elem: StructElem | None = ...

class OutlineItemDictionary(PDFObject):
    title: PDFString
    parent: Incomplete | None
    prev: Incomplete | None
    next: Incomplete | None
    first: Incomplete | None
    last: Incomplete | None
    count: int
    dest: Destination | None
    struct_elem: StructElem | None
    def __init__(self, title: str, dest: Destination | None = None, struct_elem: StructElem | None = None) -> None: ...

class OutlineDictionary(PDFObject):
    type: str
    first: Incomplete | None
    last: Incomplete | None
    count: int
    def __init__(self) -> None: ...

def build_outline_objs(
    sections: Iterable[Incomplete],
) -> Generator[Incomplete, None, list[OutlineDictionary | OutlineItemDictionary]]: ...
