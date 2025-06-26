from collections.abc import Callable, Iterable
from typing import Any, TypedDict, TypeVar
from typing_extensions import NotRequired

TYPES_MAP: dict[str, str]
REVERSE_TYPES_MAP: dict[str, tuple[str, ...]]

class _Signature(TypedDict):
    types: list[str]
    variadic: NotRequired[bool]

_F = TypeVar("_F", bound=Callable[..., Any])

def signature(*arguments: _Signature) -> Callable[[_F], _F]: ...

class FunctionRegistry(type):
    def __init__(cls, name, bases, attrs) -> None: ...

class Functions(metaclass=FunctionRegistry):
    FUNCTION_TABLE: Any
    # resolved_args and return value are the *args and return of a function called by name
    def call_function(self, function_name: str, resolved_args: Iterable[Any]) -> Any: ...
