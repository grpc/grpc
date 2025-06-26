import enum
from typing import (
    Any,
    Callable,
    ClassVar,
    Dict,
    Final,
    Iterable,
    Literal,
    Mapping,
    Optional,
    Tuple,
    Type,
    TypeVar,
    Union,
    overload,
)

from typing_extensions import dataclass_transform, Buffer

from . import inspect, json, msgpack, structs, toml, yaml

T = TypeVar("T")

class UnsetType(enum.Enum):
    UNSET = "UNSET"

UNSET = UnsetType.UNSET

class _NoDefault(enum.Enum):
    NODEFAULT = "NODEFAULT"

NODEFAULT = _NoDefault.NODEFAULT

@overload
def field(*, default: T, name: Optional[str] = None) -> T: ...
@overload
def field(*, default_factory: Callable[[], T], name: Optional[str] = None) -> T: ...
@overload
def field(*, name: Optional[str] = None) -> Any: ...
@dataclass_transform(field_specifiers=(field,))
class Struct:
    __struct_fields__: ClassVar[Tuple[str, ...]]
    __struct_config__: ClassVar[structs.StructConfig]
    __match_args__: ClassVar[Tuple[str, ...]]
    # A default __init__ so that Structs with unknown field types (say
    # constructed by `defstruct`) won't error on every call to `__init__`
    def __init__(self, *args: Any, **kwargs: Any) -> None: ...
    def __init_subclass__(
        cls,
        tag: Union[None, bool, str, int, Callable[[str], Union[str, int]]] = None,
        tag_field: Union[None, str] = None,
        rename: Union[
            None,
            Literal["lower", "upper", "camel", "pascal", "kebab"],
            Callable[[str], Optional[str]],
            Mapping[str, str],
        ] = None,
        omit_defaults: bool = False,
        forbid_unknown_fields: bool = False,
        frozen: bool = False,
        eq: bool = True,
        order: bool = False,
        kw_only: bool = False,
        repr_omit_defaults: bool = False,
        array_like: bool = False,
        gc: bool = True,
        weakref: bool = False,
        dict: bool = False,
        cache_hash: bool = False,
    ) -> None: ...
    def __rich_repr__(
        self,
    ) -> Iterable[Union[Any, Tuple[Any], Tuple[str, Any], Tuple[str, Any, Any]]]: ...

def defstruct(
    name: str,
    fields: Iterable[Union[str, Tuple[str, type], Tuple[str, type, Any]]],
    *,
    bases: Optional[Tuple[Type[Struct], ...]] = None,
    module: Optional[str] = None,
    namespace: Optional[Dict[str, Any]] = None,
    tag: Union[None, bool, str, int, Callable[[str], Union[str, int]]] = None,
    tag_field: Union[None, str] = None,
    rename: Union[
        None,
        Literal["lower", "upper", "camel", "pascal", "kebab"],
        Callable[[str], Optional[str]],
        Mapping[str, str],
    ] = None,
    omit_defaults: bool = False,
    forbid_unknown_fields: bool = False,
    frozen: bool = False,
    eq: bool = True,
    order: bool = False,
    kw_only: bool = False,
    repr_omit_defaults: bool = False,
    array_like: bool = False,
    gc: bool = True,
    weakref: bool = False,
    dict: bool = False,
    cache_hash: bool = False,
) -> Type[Struct]: ...

# Lie and say `Raw` is a subclass of `bytes`, so mypy will accept it in most
# places where an object that implements the buffer protocol is valid
class Raw(bytes):
    @overload
    def __new__(cls) -> "Raw": ...
    @overload
    def __new__(cls, msg: Union[Buffer, str]) -> "Raw": ...
    def copy(self) -> "Raw": ...

class Meta:
    def __init__(
        self,
        *,
        gt: Union[int, float, None] = None,
        ge: Union[int, float, None] = None,
        lt: Union[int, float, None] = None,
        le: Union[int, float, None] = None,
        multiple_of: Union[int, float, None] = None,
        pattern: Union[str, None] = None,
        min_length: Union[int, None] = None,
        max_length: Union[int, None] = None,
        tz: Union[bool, None] = None,
        title: Union[str, None] = None,
        description: Union[str, None] = None,
        examples: Union[list, None] = None,
        extra_json_schema: Union[dict, None] = None,
        extra: Union[dict, None] = None,
    ): ...
    gt: Final[Union[int, float, None]]
    ge: Final[Union[int, float, None]]
    lt: Final[Union[int, float, None]]
    le: Final[Union[int, float, None]]
    multiple_of: Final[Union[int, float, None]]
    pattern: Final[Union[str, None]]
    min_length: Final[Union[int, None]]
    max_length: Final[Union[int, None]]
    tz: Final[Union[int, None]]
    title: Final[Union[str, None]]
    description: Final[Union[str, None]]
    examples: Final[Union[list, None]]
    extra_json_schema: Final[Union[dict, None]]
    extra: Final[Union[dict, None]]
    def __rich_repr__(self) -> Iterable[Tuple[str, Any]]: ...

def to_builtins(
    obj: Any,
    *,
    str_keys: bool = False,
    builtin_types: Union[Iterable[type], None] = None,
    enc_hook: Optional[Callable[[Any], Any]] = None,
    order: Literal[None, "deterministic", "sorted"] = None,
) -> Any: ...
@overload
def convert(
    obj: Any,
    type: Type[T],
    *,
    strict: bool = True,
    from_attributes: bool = False,
    dec_hook: Optional[Callable[[type, Any], Any]] = None,
    builtin_types: Union[Iterable[type], None] = None,
    str_keys: bool = False,
) -> T: ...
@overload
def convert(
    obj: Any,
    type: Any,
    *,
    strict: bool = True,
    from_attributes: bool = False,
    dec_hook: Optional[Callable[[type, Any], Any]] = None,
    builtin_types: Union[Iterable[type], None] = None,
    str_keys: bool = False,
) -> Any: ...

class MsgspecError(Exception): ...
class EncodeError(MsgspecError): ...
class DecodeError(MsgspecError): ...
class ValidationError(DecodeError): ...

__version__: str
