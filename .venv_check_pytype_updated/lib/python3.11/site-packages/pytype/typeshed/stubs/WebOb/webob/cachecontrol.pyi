from collections.abc import Callable, MutableMapping
from typing import Any, Generic, Literal, TypeVar, overload
from typing_extensions import Self, TypeAlias

from webob.request import Request
from webob.response import Response

_T = TypeVar("_T")
_KT = TypeVar("_KT")
_VT = TypeVar("_VT")
_NoneLiteral = TypeVar("_NoneLiteral")
_Type: TypeAlias = type

class UpdateDict(dict[_KT, _VT]):
    updated: Callable[..., Any] | None
    updated_args: tuple[Any, ...] | None

class exists_property:
    prop: str
    type: str | None
    def __init__(self, prop: str, type: str | None = None) -> None: ...
    @overload
    def __get__(self, obj: None, type: _Type | None = None) -> Self: ...
    @overload
    def __get__(self, obj: Any, type: _Type | None = None) -> bool: ...
    def __set__(self, obj: Any, value: bool | None) -> None: ...
    def __delete__(self, obj: Any) -> None: ...

class value_property(Generic[_T, _NoneLiteral]):
    prop: str
    default: _T | None
    none: _NoneLiteral
    type: str | None
    @overload
    def __init__(self, prop: str, default: None = None, none: None = None, type: str | None = None) -> None: ...
    @overload
    def __init__(self, prop: str, default: _T, none: _NoneLiteral, type: str | None = None) -> None: ...
    @overload
    def __get__(self, obj: None, type: _Type | None = None) -> Self: ...
    @overload
    def __get__(self, obj: Any, type: _Type | None = None) -> _T | _NoneLiteral | None: ...
    def __set__(self, obj: Any, value: _T | Literal[True] | None) -> None: ...
    def __delete__(self, obj: Any) -> None: ...

class _IntValueProperty(value_property[int, _NoneLiteral]):
    def __set__(self, obj: Any, value: int | None) -> None: ...

class _BaseCacheControl:
    update_dict = UpdateDict
    properties: MutableMapping[str, Any]
    type: Literal["request", "response"] | None
    @classmethod
    @overload
    def parse(cls, header: str, updates_to: None = None, type: None = None) -> _AnyCacheControl: ...
    @classmethod
    @overload
    def parse(cls, header: str, updates_to: Request, type: Literal["request"]) -> _RequestCacheControl: ...
    @classmethod
    @overload
    def parse(cls, header: str, updates_to: Response, type: Literal["response"]) -> _ResponseCacheControl: ...

    no_cache: value_property[str, Literal["*"]]
    no_store: exists_property
    no_transform: exists_property
    max_age: _IntValueProperty[Literal[-1]]
    def copy(self) -> Self: ...

class _RequestCacheControl(_BaseCacheControl):
    type: Literal["request"]
    max_stale: _IntValueProperty[Literal["*"]]
    min_fresh: _IntValueProperty[None]
    only_if_cached: exists_property

class _ResponseCacheControl(_BaseCacheControl):
    type: Literal["response"]
    public: exists_property
    private: value_property[str, Literal["*"]]
    must_revalidate: exists_property
    proxy_revalidate: exists_property
    s_maxage: _IntValueProperty[None]
    s_max_age: _IntValueProperty[None]
    stale_while_revalidate: _IntValueProperty[None]
    stale_if_error: _IntValueProperty[None]

class _AnyCacheControl(_RequestCacheControl, _ResponseCacheControl):
    type: None  # type: ignore[assignment]

class CacheControl(_AnyCacheControl):
    @overload
    def __init__(self: _AnyCacheControl, properties: MutableMapping[str, Any], type: None) -> None: ...
    @overload
    def __init__(self: _RequestCacheControl, properties: MutableMapping[str, Any], type: Literal["request"]) -> None: ...
    @overload
    def __init__(self: _ResponseCacheControl, properties: MutableMapping[str, Any], type: Literal["response"]) -> None: ...

def serialize_cache_control(properties: MutableMapping[str, Any] | _BaseCacheControl) -> str: ...
