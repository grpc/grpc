from _typeshed import sentinel
from _typeshed.wsgi import WSGIEnvironment
from collections.abc import Callable, Collection, ItemsView, Iterator, KeysView, MutableMapping, ValuesView
from datetime import date, datetime, timedelta
from hashlib import _Hash
from typing import Any, Literal, Protocol, TypeVar, overload
from typing_extensions import TypeAlias

from webob.descriptors import _AsymmetricProperty
from webob.request import Request
from webob.response import Response

_T = TypeVar("_T")
# we accept both the official spelling and the one used in the WebOb docs
# the implementation compares after lower() so technically there are more
# valid spellings, but it seems more natural to support these two spellings
_SameSitePolicy: TypeAlias = Literal["Strict", "Lax", "None", "strict", "lax", "none"]

class _Serializer(Protocol):
    def loads(self, appstruct: Any, /) -> bytes: ...
    def dumps(self, bstruct: bytes, /) -> Any: ...

class RequestCookies(MutableMapping[str, str]):
    def __init__(self, environ: WSGIEnvironment) -> None: ...
    def __setitem__(self, name: str, value: str) -> None: ...
    def __getitem__(self, name: str) -> str: ...
    @overload
    def get(self, name: str, default: None = None) -> str | None: ...
    @overload
    def get(self, name: str, default: str | _T) -> str | _T: ...
    def __delitem__(self, name: str) -> None: ...
    def keys(self) -> KeysView[str]: ...
    def values(self) -> ValuesView[str]: ...
    def items(self) -> ItemsView[str, str]: ...
    def __contains__(self, name: object) -> bool: ...
    def __iter__(self) -> Iterator[str]: ...
    def __len__(self) -> int: ...
    def clear(self) -> None: ...

class Cookie(dict[str, Morsel]):
    def __init__(self, input: str | None = None) -> None: ...
    def load(self, data: str) -> None: ...
    def add(self, key: str | bytes, val: str | bytes) -> Morsel: ...
    def __setitem__(self, key: str | bytes, val: str | bytes) -> Morsel: ...  # type: ignore[override]
    def serialize(self, full: bool = True) -> str: ...
    def values(self) -> list[Morsel]: ...  # type: ignore[override]
    def __str__(self, full: bool = True) -> str: ...

class Morsel(dict[bytes, bytes | bool | None]):
    name: bytes
    value: bytes
    def __init__(self, name: str | bytes, value: str | bytes) -> None: ...
    @property
    def path(self) -> bytes | None: ...
    @path.setter
    def path(self, v: bytes | None) -> None: ...
    @property
    def domain(self) -> bytes | None: ...
    @domain.setter
    def domain(self, v: bytes | None) -> None: ...
    @property
    def comment(self) -> bytes | None: ...
    @comment.setter
    def comment(self, v: bytes | None) -> None: ...
    expires: _AsymmetricProperty[bytes | None, datetime | date | timedelta | int | str | bytes | None]
    max_age: _AsymmetricProperty[bytes | None, timedelta | int | str | bytes]
    @property
    def httponly(self) -> bool | None: ...
    @httponly.setter
    def httponly(self, v: bool) -> None: ...
    @property
    def secure(self) -> bool | None: ...
    @secure.setter
    def secure(self, v: bool) -> None: ...
    samesite: _AsymmetricProperty[bytes | None, _SameSitePolicy | None]
    def serialize(self, full: bool = True) -> str: ...
    def __str__(self, full: bool = True) -> str: ...

def make_cookie(
    name: str | bytes,
    value: str | bytes | None,
    max_age: int | timedelta | None = None,
    path: str = "/",
    domain: str | None = None,
    secure: bool = False,
    httponly: bool = False,
    comment: str | None = None,
    samesite: _SameSitePolicy | None = None,
) -> str: ...

class JSONSerializer:
    def dumps(self, appstruct: Any) -> bytes: ...
    def loads(self, bstruct: bytes | str) -> Any: ...

class Base64Serializer:
    serializer: _Serializer
    def __init__(self, serializer: _Serializer | None = None) -> None: ...
    def dumps(self, appstruct: Any) -> bytes: ...
    def loads(self, bstruct: bytes) -> Any: ...

class SignedSerializer:
    salt: str
    secret: str
    hashalg: str
    salted_secret: bytes
    digestmod: Callable[[bytes], _Hash]
    digest_size: int
    serializer: _Serializer
    def __init__(self, secret: str, salt: str, hashalg: str = "sha512", serializer: _Serializer | None = None) -> None: ...
    def dumps(self, appstruct: Any) -> bytes: ...
    def loads(self, bstruct: bytes) -> Any: ...

class CookieProfile:
    cookie_name: str
    secure: bool
    max_age: int | timedelta | None
    httponly: bool | None
    samesite: _SameSitePolicy | None
    path: str
    domains: Collection[str] | None
    serializer: _Serializer
    request: Request | None
    def __init__(
        self,
        cookie_name: str,
        secure: bool = False,
        max_age: int | timedelta | None = None,
        httponly: bool | None = None,
        samesite: _SameSitePolicy | None = None,
        path: str = "/",
        # even though the docs claim any iterable is fine, that is
        # clearly not the case judging by the implementation
        domains: Collection[str] | None = None,
        serializer: _Serializer | None = None,
    ) -> None: ...
    def __call__(self, request: Request) -> CookieProfile: ...
    def bind(self, request: Request) -> CookieProfile: ...
    def get_value(self) -> Any | None: ...
    def set_cookies(
        self,
        response: Response,
        value: Any,
        domains: Collection[str] = sentinel,
        max_age: int | timedelta | None = sentinel,
        path: str = sentinel,
        secure: bool = sentinel,
        httponly: bool = sentinel,
        samesite: _SameSitePolicy | None = sentinel,
    ) -> Response: ...
    def get_headers(
        self,
        value: Any,
        domains: Collection[str] = sentinel,
        max_age: int | timedelta | None = sentinel,
        path: str = sentinel,
        secure: bool = sentinel,
        httponly: bool = sentinel,
        samesite: _SameSitePolicy | None = sentinel,
    ) -> list[tuple[str, str]]: ...

class SignedCookieProfile(CookieProfile):
    secret: str
    salt: str
    hashalg: str
    serializer: SignedSerializer
    original_serializer: _Serializer
    def __init__(
        self,
        secret: str,
        salt: str,
        cookie_name: str,
        secure: bool = False,
        max_age: int | timedelta | None = None,
        httponly: bool = False,
        samesite: _SameSitePolicy | None = None,
        path: str = "/",
        domains: Collection[str] | None = None,
        hashalg: str = "sha512",
        serializer: _Serializer | None = None,
    ) -> None: ...
    def __call__(self, request: Request) -> SignedCookieProfile: ...
    def bind(self, request: Request) -> SignedCookieProfile: ...
