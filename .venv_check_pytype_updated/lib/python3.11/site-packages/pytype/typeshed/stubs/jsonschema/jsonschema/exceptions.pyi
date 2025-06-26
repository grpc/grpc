from _typeshed import Incomplete, SupportsRichComparison
from collections import deque
from collections.abc import Callable, Container, Iterable, Iterator, MutableMapping, Sequence
from typing_extensions import Self, TypeAlias, deprecated

from jsonschema import _utils, protocols
from jsonschema._types import TypeChecker

_RelevanceFuncType: TypeAlias = Callable[[ValidationError], SupportsRichComparison]

WEAK_MATCHES: frozenset[str]
STRONG_MATCHES: frozenset[str]

class _Error(Exception):
    message: str
    path: deque[str | int]
    relative_path: deque[str | int]
    schema_path: deque[str | int]
    relative_schema_path: deque[str | int]
    context: list[ValidationError] | None
    cause: Exception | None
    validator: protocols.Validator | None
    validator_value: Incomplete
    instance: Incomplete
    schema: Incomplete
    parent: _Error | None
    def __init__(
        self,
        message: str,
        validator: _utils.Unset | None | protocols.Validator = ...,
        path: Sequence[str | int] = (),
        cause: Incomplete | None = None,
        context: Sequence[ValidationError] = (),
        validator_value=...,
        instance: Incomplete = ...,
        schema: Incomplete = ...,
        schema_path: Sequence[str | int] = (),
        parent: _Error | None = None,
        type_checker: _utils.Unset | TypeChecker = ...,
    ) -> None: ...
    @classmethod
    def create_from(cls, other: _Error) -> Self: ...
    @property
    def absolute_path(self) -> Sequence[str | int]: ...
    @property
    def absolute_schema_path(self) -> Sequence[str | int]: ...
    @property
    def json_path(self) -> str: ...
    # TODO: this type could be made more precise using TypedDict to
    # enumerate the types of the members
    def _contents(self) -> dict[str, Incomplete]: ...

class ValidationError(_Error): ...
class SchemaError(_Error): ...

class RefResolutionError(Exception):
    def __init__(self, cause: str) -> None: ...

class UndefinedTypeCheck(Exception):
    type: Incomplete
    def __init__(self, type) -> None: ...

class UnknownType(Exception):
    type: Incomplete
    instance: Incomplete
    schema: Incomplete
    def __init__(self, type, instance, schema) -> None: ...

class FormatError(Exception):
    message: Incomplete
    cause: Incomplete
    def __init__(self, message, cause: Incomplete | None = None) -> None: ...

class ErrorTree:
    errors: MutableMapping[str, ValidationError]
    def __init__(self, errors: Iterable[ValidationError] = ()) -> None: ...
    def __contains__(self, index: object) -> bool: ...
    def __getitem__(self, index): ...
    @deprecated("ErrorTree.__setitem__ is deprecated without replacement.")
    def __setitem__(self, index: str | int, value: ErrorTree) -> None: ...
    def __iter__(self) -> Iterator[str]: ...
    def __len__(self) -> int: ...
    @property
    def total_errors(self): ...

def by_relevance(weak: Container[str] = ..., strong: Container[str] = ...) -> _RelevanceFuncType: ...

relevance: _RelevanceFuncType

def best_match(errors: Iterable[ValidationError], key: _RelevanceFuncType = ...): ...
