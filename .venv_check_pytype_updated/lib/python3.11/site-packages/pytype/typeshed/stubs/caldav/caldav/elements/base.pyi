from _typeshed import Incomplete
from collections.abc import Iterable
from typing import Any, ClassVar
from typing_extensions import Self, TypeAlias

_Element: TypeAlias = Any  # actually lxml.etree._Element

class BaseElement:
    tag: ClassVar[str | None]
    children: list[BaseElement]
    value: str | None
    attributes: Incomplete | None
    caldav_class: Incomplete | None
    def __init__(self, name: str | None = None, value: str | bytes | None = None) -> None: ...
    def __add__(self, other: BaseElement) -> Self: ...
    def xmlelement(self) -> _Element: ...
    def xmlchildren(self, root: _Element) -> None: ...
    def append(self, element: BaseElement | Iterable[BaseElement]) -> Self: ...

class NamedBaseElement(BaseElement):
    def __init__(self, name: str | None = None) -> None: ...

class ValuedBaseElement(BaseElement):
    def __init__(self, value: str | bytes | None = None) -> None: ...
