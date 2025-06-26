from _typeshed import Incomplete, Unused
from collections.abc import Generator
from typing import ClassVar, Literal, TypeVar, overload
from zipfile import ZipFile

from openpyxl.descriptors.base import Alias, String
from openpyxl.descriptors.serialisable import Serialisable
from openpyxl.pivot.cache import CacheDefinition
from openpyxl.pivot.record import RecordList
from openpyxl.pivot.table import TableDefinition
from openpyxl.xml.functions import Element

_SerialisableT = TypeVar("_SerialisableT", bound=Serialisable)
_SerialisableRelTypeT = TypeVar("_SerialisableRelTypeT", bound=CacheDefinition | RecordList | TableDefinition)

class Relationship(Serialisable):
    tagname: ClassVar[str]
    Type: String[Literal[False]]
    Target: String[Literal[False]]
    target: Alias
    TargetMode: String[Literal[True]]
    Id: String[Literal[True]]
    id: Alias
    @overload
    def __init__(
        self, Id: str, Type: Unused = None, *, type: str, Target: str | None = None, TargetMode: str | None = None
    ) -> None: ...
    @overload
    def __init__(self, Id: str, Type: Unused, type: str, Target: str | None = None, TargetMode: str | None = None) -> None: ...
    @overload
    def __init__(
        self, Id: str, Type: str, type: None = None, Target: str | None = None, TargetMode: str | None = None
    ) -> None: ...

class RelationshipList(Serialisable):
    tagname: ClassVar[str]
    Relationship: Incomplete
    def __init__(self, Relationship=()) -> None: ...
    def append(self, value) -> None: ...
    def __len__(self) -> int: ...
    def __bool__(self) -> bool: ...
    def find(self, content_type) -> Generator[Incomplete, None, None]: ...
    def __getitem__(self, key): ...
    def to_tree(self) -> Element: ...  # type: ignore[override]

def get_rels_path(path): ...
def get_dependents(archive: ZipFile, filename: str) -> RelationshipList: ...

# If `id` is None, `cls` needs to have ClassVar `rel_type`.
# The `deps` attribute used at runtime is for internal use immediately after the return.
# `cls` cannot be None
@overload
def get_rel(
    archive: ZipFile, deps: RelationshipList, id: None = None, *, cls: type[_SerialisableRelTypeT]
) -> _SerialisableRelTypeT | None: ...
@overload
def get_rel(
    archive: ZipFile, deps: RelationshipList, id: None, cls: type[_SerialisableRelTypeT]
) -> _SerialisableRelTypeT | None: ...
@overload
def get_rel(archive: ZipFile, deps: RelationshipList, id: str, cls: type[_SerialisableT]) -> _SerialisableT: ...
