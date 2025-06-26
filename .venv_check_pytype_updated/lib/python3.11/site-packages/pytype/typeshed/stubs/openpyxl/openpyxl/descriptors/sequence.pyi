from _typeshed import Incomplete, Unused
from collections.abc import Generator, Iterable, Sized
from typing import Any, Protocol, TypeVar
from typing_extensions import Self

from openpyxl.descriptors import Strict
from openpyxl.descriptors.serialisable import Serialisable, _SerialisableTreeElement
from openpyxl.xml._functions_overloads import _HasGet
from openpyxl.xml.functions import Element

from .base import Alias, Descriptor

_T = TypeVar("_T")

class _SupportsFromTree(Protocol):
    @classmethod
    def from_tree(cls, node: _SerialisableTreeElement) -> Any: ...

class _SupportsToTree(Protocol):
    def to_tree(self) -> Element: ...

class Sequence(Descriptor[Incomplete]):
    expected_type: type[Incomplete]
    seq_types: tuple[type, ...]
    idx_base: int
    unique: bool
    container: type
    def __set__(self, instance: Serialisable | Strict, seq) -> None: ...
    def to_tree(
        self, tagname: str | None, obj: Iterable[object], namespace: str | None = None
    ) -> Generator[Element, None, None]: ...

class UniqueSequence(Sequence):
    seq_types: tuple[type, ...]
    container: type

class ValueSequence(Sequence):
    attribute: str
    def to_tree(
        self, tagname: str, obj: Iterable[object], namespace: str | None = None  # type: ignore[override]
    ) -> Generator[Element, None, None]: ...
    def from_tree(self, node: _HasGet[_T]) -> _T: ...

class _NestedSequenceToTreeObj(Sized, Iterable[_SupportsToTree], Protocol): ...

class NestedSequence(Sequence):
    count: bool
    expected_type: type[_SupportsFromTree]
    def to_tree(  # type: ignore[override]
        self, tagname: str, obj: _NestedSequenceToTreeObj, namespace: str | None = None
    ) -> Element: ...
    # returned list generic type should be same as the return type of expected_type.from_tree(node)
    # Which can really be anything given the wildly different, and sometimes generic, from_tree return types
    def from_tree(self, node: Iterable[_SerialisableTreeElement]) -> list[Any]: ...

class MultiSequence(Sequence):
    def __set__(self, instance: Serialisable | Strict, seq) -> None: ...
    def to_tree(
        self, tagname: Unused, obj: Iterable[_SupportsToTree], namespace: str | None = None  # type: ignore[override]
    ) -> Generator[Element, None, None]: ...

class MultiSequencePart(Alias):
    expected_type: type[Incomplete]
    store: Incomplete
    def __init__(self, expected_type, store) -> None: ...
    def __set__(self, instance: Serialisable | Strict, value) -> None: ...
    def __get__(self, instance: Unused, cls: Unused) -> Self: ...
