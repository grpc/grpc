import enum
from _typeshed import ConvertibleToFloat, SupportsKeysAndGetItem
from collections.abc import Callable, Iterable, Iterator, Mapping, Sequence, Set as AbstractSet
from typing import Any, Generic, Literal, NamedTuple, TypeVar, overload, type_check_only
from typing_extensions import ParamSpec, Self, TypeAlias

_T = TypeVar("_T")
_KT = TypeVar("_KT")
_VT_co = TypeVar("_VT_co", covariant=True)
_P = ParamSpec("_P")
_Mode: TypeAlias = Literal["string", "filename", "dirname"]
_OutputStyle: TypeAlias = Literal["nested", "expanded", "compact", "compressed"]
_CustomFunctions: TypeAlias = Mapping[str, Callable[..., Any]] | Sequence[Callable[..., Any]] | AbstractSet[Callable[..., Any]]
_ImportCallbackRet: TypeAlias = (
    list[tuple[str, str, str]] | list[tuple[str, str]] | list[tuple[str]] | list[tuple[str, ...]] | None
)
_ImportCallback: TypeAlias = Callable[[str], _ImportCallbackRet] | Callable[[str, str], _ImportCallbackRet]

__version__: str
libsass_version: str
OUTPUT_STYLES: dict[str, int]
SOURCE_COMMENTS: dict[str, int]
MODES: frozenset[_Mode]

class CompileError(ValueError):
    def __init__(self, msg: str) -> None: ...

# _P needs to be positional only and can't contain varargs, but there is no way to express that
# the arguments also need
class SassFunction(Generic[_P, _T]):
    @classmethod
    def from_lambda(cls, name: str, lambda_: Callable[_P, _T]) -> SassFunction[_P, _T]: ...
    @classmethod
    def from_named_function(cls, function: Callable[_P, _T]) -> SassFunction[_P, _T]: ...
    name: str
    arguments: tuple[str, ...]
    callable_: Callable[_P, _T]
    def __init__(self, name: str, arguments: Sequence[str], callable_: Callable[_P, _T]) -> None: ...
    @property
    def signature(self) -> str: ...
    def __call__(self, *args: _P.args, **kwargs: _P.kwargs) -> _T: ...

@overload
def compile(
    *,
    string: str,
    output_style: _OutputStyle = "nested",
    source_comments: bool = False,
    source_map_contents: bool = False,
    source_map_embed: bool = False,
    omit_source_map_url: bool = False,
    source_map_root: str | None = None,
    include_paths: Sequence[str] = (),
    precision: int = 5,
    custom_functions: _CustomFunctions = (),
    indented: bool = False,
    importers: Iterable[tuple[int, _ImportCallback]] | None = None,
) -> str: ...
@overload
def compile(
    *,
    filename: str,
    output_style: _OutputStyle = "nested",
    source_comments: bool = False,
    source_map_filename: None = None,
    output_filename_hint: str | None = None,
    source_map_contents: bool = False,
    source_map_embed: bool = False,
    omit_source_map_url: bool = False,
    source_map_root: str | None = None,
    include_paths: Sequence[str] = (),
    precision: int = 5,
    custom_functions: _CustomFunctions = (),
    importers: Iterable[tuple[int, _ImportCallback]] | None = None,
) -> str: ...
@overload
def compile(
    *,
    filename: str,
    output_style: _OutputStyle = "nested",
    source_comments: bool = False,
    source_map_filename: str,
    output_filename_hint: str | None = None,
    source_map_contents: bool = False,
    source_map_embed: bool = False,
    omit_source_map_url: bool = False,
    source_map_root: str | None = None,
    include_paths: Sequence[str] = (),
    precision: int = 5,
    custom_functions: _CustomFunctions = (),
    importers: Iterable[tuple[int, _ImportCallback]] | None = None,
) -> tuple[str, str]: ...
@overload
def compile(
    *,
    dirname: tuple[str, str],
    output_style: _OutputStyle = "nested",
    source_comments: bool = False,
    source_map_contents: bool = False,
    source_map_embed: bool = False,
    omit_source_map_url: bool = False,
    source_map_root: str | None = None,
    include_paths: Sequence[str] = (),
    precision: int = 5,
    custom_functions: _CustomFunctions = (),
    importers: Iterable[tuple[int, _ImportCallback]] | None = None,
) -> None: ...
def and_join(strings: Sequence[str]) -> str: ...
@type_check_only
class _SassNumber(NamedTuple):
    value: float
    unit: str

class SassNumber(_SassNumber):
    def __new__(cls, value: ConvertibleToFloat, unit: str | bytes) -> Self: ...

@type_check_only
class _SassColor(NamedTuple):
    r: float
    g: float
    b: float
    a: float

class SassColor(_SassColor):
    def __new__(cls, r: ConvertibleToFloat, g: ConvertibleToFloat, b: ConvertibleToFloat, a: ConvertibleToFloat) -> Self: ...

@type_check_only
class _Separator(enum.Enum):
    SASS_SEPARATOR_COMMA = enum.auto()
    SASS_SEPARATOR_SPACE = enum.auto()

SASS_SEPARATOR_COMMA: Literal[_Separator.SASS_SEPARATOR_COMMA]
SASS_SEPARATOR_SPACE: Literal[_Separator.SASS_SEPARATOR_SPACE]

@type_check_only
class _SassList(NamedTuple, Generic[_T]):
    items: tuple[_T, ...]
    separator: _Separator
    bracketed: bool

class SassList(_SassList[_T]):
    def __new__(cls, items: Iterable[_T], separator: _Separator, bracketed: bool = ...) -> SassList[_T]: ...

@type_check_only
class _SassError(NamedTuple):
    msg: str

class SassError(_SassError):
    def __new__(cls, msg: str | bytes) -> Self: ...

@type_check_only
class _SassWarning(NamedTuple):
    msg: str

class SassWarning(_SassWarning):
    def __new__(cls, msg: str | bytes) -> Self: ...

class SassMap(Mapping[_KT, _VT_co]):
    # copied from dict.__init__ in builtins.pyi, since it uses dict() internally
    @overload
    def __init__(self) -> None: ...
    @overload
    def __init__(self: SassMap[str, _VT_co], **kwargs: _VT_co) -> None: ...
    @overload
    def __init__(self, map: SupportsKeysAndGetItem[_KT, _VT_co], /) -> None: ...
    @overload
    def __init__(self: SassMap[str, _VT_co], map: SupportsKeysAndGetItem[str, _VT_co], /, **kwargs: _VT_co) -> None: ...
    @overload
    def __init__(self, iterable: Iterable[tuple[_KT, _VT_co]], /) -> None: ...
    @overload
    def __init__(self: SassMap[str, _VT_co], iterable: Iterable[tuple[str, _VT_co]], /, **kwargs: _VT_co) -> None: ...
    def __getitem__(self, key: _KT) -> _VT_co: ...
    def __iter__(self) -> Iterator[_KT]: ...
    def __len__(self) -> int: ...
    def __hash__(self) -> int: ...
