from _typeshed import Incomplete
from collections.abc import Iterator, Mapping as DictMixin

class LazyDict(DictMixin[str, Incomplete]):
    data: dict[str, Incomplete] | None
    def __getitem__(self, key: str) -> Incomplete: ...
    def __contains__(self, key: object) -> bool: ...
    def __iter__(self) -> Iterator[str]: ...
    def __len__(self) -> int: ...

class LazyList(list[Incomplete]):
    # does not return `Self` type:
    def __new__(cls, fill_iter: Incomplete | None = None) -> LazyList: ...  # noqa: Y034

class LazySet(set[Incomplete]):
    # does not return `Self` type:
    def __new__(cls, fill_iter: Incomplete | None = None) -> LazySet: ...  # noqa: Y034
