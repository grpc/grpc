from _typeshed import SupportsRichComparison
from collections.abc import Callable
from typing import IO, Any, TypeVar, overload
from typing_extensions import TypeAlias

from simplejson.decoder import JSONDecoder as JSONDecoder
from simplejson.encoder import JSONEncoder as JSONEncoder, JSONEncoderForHTML as JSONEncoderForHTML
from simplejson.raw_json import RawJSON as RawJSON
from simplejson.scanner import JSONDecodeError as JSONDecodeError

_LoadsString: TypeAlias = str | bytes | bytearray
_T = TypeVar("_T")

@overload
def dumps(
    obj: Any,
    skipkeys: bool = ...,
    ensure_ascii: bool = ...,
    check_circular: bool = ...,
    allow_nan: bool = ...,
    *,
    cls: type[JSONEncoder],
    indent: str | int | None = ...,
    separators: tuple[str, str] | None = ...,
    encoding: str = ...,
    default: Callable[[Any], Any] | None = ...,
    use_decimal: bool = ...,
    namedtuple_as_object: bool = ...,
    tuple_as_array: bool = ...,
    bigint_as_string: bool = ...,
    sort_keys: bool = ...,
    item_sort_key: Callable[[Any], SupportsRichComparison] | None = ...,
    for_json: bool = ...,
    ignore_nan: bool = ...,
    int_as_string_bitcount: int | None = ...,
    iterable_as_array: bool = ...,
    **kw: Any,
) -> str: ...
@overload
def dumps(
    obj: Any,
    skipkeys: bool = ...,
    ensure_ascii: bool = ...,
    check_circular: bool = ...,
    allow_nan: bool = ...,
    cls: type[JSONEncoder] | None = ...,
    indent: str | int | None = ...,
    separators: tuple[str, str] | None = ...,
    encoding: str = ...,
    default: Callable[[Any], Any] | None = ...,
    use_decimal: bool = ...,
    namedtuple_as_object: bool = ...,
    tuple_as_array: bool = ...,
    bigint_as_string: bool = ...,
    sort_keys: bool = ...,
    item_sort_key: Callable[[Any], SupportsRichComparison] | None = ...,
    for_json: bool = ...,
    ignore_nan: bool = ...,
    int_as_string_bitcount: int | None = ...,
    iterable_as_array: bool = ...,
) -> str: ...
@overload
def dump(
    obj: Any,
    fp: IO[str],
    skipkeys: bool = ...,
    ensure_ascii: bool = ...,
    check_circular: bool = ...,
    allow_nan: bool = ...,
    *,
    cls: type[JSONEncoder],
    indent: str | int | None = ...,
    separators: tuple[str, str] | None = ...,
    encoding: str = ...,
    default: Callable[[Any], Any] | None = ...,
    use_decimal: bool = ...,
    namedtuple_as_object: bool = ...,
    tuple_as_array: bool = ...,
    bigint_as_string: bool = ...,
    sort_keys: bool = ...,
    item_sort_key: Callable[[Any], SupportsRichComparison] | None = ...,
    for_json: bool = ...,
    ignore_nan: bool = ...,
    int_as_string_bitcount: int | None = ...,
    iterable_as_array: bool = ...,
    **kw: Any,
) -> None: ...
@overload
def dump(
    obj: Any,
    fp: IO[str],
    skipkeys: bool = ...,
    ensure_ascii: bool = ...,
    check_circular: bool = ...,
    allow_nan: bool = ...,
    cls: type[JSONEncoder] | None = ...,
    indent: str | int | None = ...,
    separators: tuple[str, str] | None = ...,
    encoding: str = ...,
    default: Callable[[Any], Any] | None = ...,
    use_decimal: bool = ...,
    namedtuple_as_object: bool = ...,
    tuple_as_array: bool = ...,
    bigint_as_string: bool = ...,
    sort_keys: bool = ...,
    item_sort_key: Callable[[Any], SupportsRichComparison] | None = ...,
    for_json: bool = ...,
    ignore_nan: bool = ...,
    int_as_string_bitcount: int | None = ...,
    iterable_as_array: bool = ...,
) -> None: ...
@overload
def loads(
    s: _LoadsString,
    encoding: str | None = ...,
    *,
    cls: type[JSONDecoder],
    object_hook: Callable[[dict[Any, Any]], Any] | None = ...,
    parse_float: Callable[[str], Any] | None = ...,
    parse_int: Callable[[str], Any] | None = ...,
    parse_constant: Callable[[str], Any] | None = ...,
    object_pairs_hook: Callable[[list[tuple[Any, Any]]], Any] | None = ...,
    use_decimal: bool = ...,
    allow_nan: bool = ...,
    **kw: Any,
) -> Any: ...
@overload
def loads(
    s: _LoadsString,
    encoding: str | None = ...,
    cls: type[JSONDecoder] | None = ...,
    object_hook: Callable[[dict[Any, Any]], Any] | None = ...,
    parse_float: Callable[[str], Any] | None = ...,
    parse_int: Callable[[str], Any] | None = ...,
    parse_constant: Callable[[str], Any] | None = ...,
    object_pairs_hook: Callable[[list[tuple[Any, Any]]], Any] | None = ...,
    use_decimal: bool = ...,
    allow_nan: bool = ...,
) -> Any: ...
@overload
def load(
    fp: IO[str],
    encoding: str | None = ...,
    *,
    cls: type[JSONDecoder],
    object_hook: Callable[[dict[Any, Any]], Any] | None = ...,
    parse_float: Callable[[str], Any] | None = ...,
    parse_int: Callable[[str], Any] | None = ...,
    parse_constant: Callable[[str], Any] | None = ...,
    object_pairs_hook: Callable[[list[tuple[Any, Any]]], Any] | None = ...,
    use_decimal: bool = ...,
    allow_nan: bool = ...,
    **kw: Any,
) -> Any: ...
@overload
def load(
    fp: IO[str],
    encoding: str | None = ...,
    cls: type[JSONDecoder] | None = ...,
    object_hook: Callable[[dict[Any, Any]], Any] | None = ...,
    parse_float: Callable[[str], Any] | None = ...,
    parse_int: Callable[[str], Any] | None = ...,
    parse_constant: Callable[[str], Any] | None = ...,
    object_pairs_hook: Callable[[list[tuple[Any, Any]]], Any] | None = ...,
    use_decimal: bool = ...,
    allow_nan: bool = ...,
) -> Any: ...
def simple_first(kv: tuple[_T, object]) -> tuple[bool, _T]: ...
