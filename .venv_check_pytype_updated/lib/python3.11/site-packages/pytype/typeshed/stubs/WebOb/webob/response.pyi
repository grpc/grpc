from _typeshed import SupportsItems, SupportsRead
from _typeshed.wsgi import StartResponse, WSGIApplication, WSGIEnvironment
from collections.abc import Iterable, Iterator, Sequence
from datetime import timedelta
from typing import IO, Any, Literal, Protocol, TypedDict
from typing_extensions import TypeAlias

from webob.byterange import ContentRange
from webob.cachecontrol import _ResponseCacheControl
from webob.cookies import _SameSitePolicy
from webob.descriptors import (
    _AsymmetricProperty,
    _AsymmetricPropertyWithDelete,
    _authorization,
    _ContentRangeParams,
    _DateProperty,
    _ListProperty,
)
from webob.headers import ResponseHeaders
from webob.request import Request

class _ResponseCacheExpires(Protocol):
    def __call__(
        self,
        seconds: int | timedelta = 0,
        *,
        public: bool = ...,
        private: Literal[True] | str = ...,
        no_cache: Literal[True] | str = ...,
        no_store: bool = ...,
        no_transform: bool = ...,
        must_revalidate: bool = ...,
        proxy_revalidate: bool = ...,
        max_age: int = ...,
        s_maxage: int = ...,
        s_max_age: int = ...,
        stale_while_revalidate: int = ...,
        stale_if_error: int = ...,
    ) -> None: ...

class _ResponseCacheControlDict(TypedDict, total=False):
    public: bool
    private: Literal[True] | str
    no_cache: Literal[True] | str
    no_store: bool
    no_transform: bool
    must_revalidate: bool
    proxy_revalidate: bool
    max_age: int
    s_maxage: int
    s_max_age: int
    stale_while_revalidate: int
    stale_if_error: int

_HTTPHeader: TypeAlias = tuple[str, str]

class Response:
    default_content_type: str
    default_charset: str
    unicode_errors: str
    default_conditional_response: bool
    default_body_encoding: str
    request: Request | None
    environ: WSGIEnvironment | None
    status: str
    conditional_response: bool

    def __init__(
        self,
        body: bytes | str | None = None,
        status: str | None = None,
        headerlist: list[_HTTPHeader] | None = None,
        app_iter: Iterator[bytes] | None = None,
        content_type: str | None = None,
        conditional_response: bool | None = None,
        charset: str = ...,
        **kw: Any,
    ) -> None: ...
    @classmethod
    def from_file(cls, fp: IO[str]) -> Response: ...
    def copy(self) -> Response: ...
    status_code: int
    status_int: int
    headerlist: _AsymmetricPropertyWithDelete[list[_HTTPHeader], Iterable[_HTTPHeader] | SupportsItems[str, str]]
    headers: _AsymmetricProperty[ResponseHeaders, SupportsItems[str, str] | Iterable[tuple[str, str]]]
    body: bytes
    json: Any
    json_body: Any
    @property
    def has_body(self) -> bool: ...
    text: str
    unicode_body: str  # deprecated
    ubody: str  # deprecated
    body_file: _AsymmetricPropertyWithDelete[ResponseBodyFile, SupportsRead[bytes]]
    content_length: int | None
    def write(self, text: str | bytes) -> None: ...
    app_iter: Iterator[bytes]
    allow: _ListProperty
    vary: _ListProperty
    content_encoding: str | None
    content_language: _ListProperty
    content_location: str | None
    content_md5: str | None
    content_disposition: str | None
    accept_ranges: str | None
    content_range: _AsymmetricPropertyWithDelete[ContentRange | None, _ContentRangeParams]
    date: _DateProperty
    expires: _DateProperty
    last_modified: _DateProperty
    etag: _AsymmetricPropertyWithDelete[str | None, tuple[str, bool] | str | None]
    @property
    def etag_strong(self) -> str | None: ...
    location: str | None
    pragma: str | None
    age: int | None
    retry_after: _DateProperty
    server: str | None
    www_authenticate: _AsymmetricPropertyWithDelete[
        _authorization | None, tuple[str, str | dict[str, str]] | list[Any] | str | None
    ]
    charset: str | None
    content_type: str | None
    content_type_params: _AsymmetricPropertyWithDelete[dict[str, str], SupportsItems[str, str] | None]
    def set_cookie(
        self,
        name: str,
        value: str | None = "",
        max_age: int | timedelta | None = None,
        path: str = "/",
        domain: str | None = None,
        secure: bool = False,
        httponly: bool = False,
        comment: str | None = None,
        overwrite: bool = False,
        samesite: _SameSitePolicy | None = None,
    ) -> None: ...
    def delete_cookie(self, name: str, path: str = "/", domain: str | None = None) -> None: ...
    def unset_cookie(self, name: str, strict: bool = True) -> None: ...
    def merge_cookies(self, resp: Response | WSGIApplication) -> None: ...
    cache_control: _AsymmetricProperty[_ResponseCacheControl, _ResponseCacheControl | _ResponseCacheControlDict | str | None]
    cache_expires: _AsymmetricProperty[_ResponseCacheExpires, timedelta | int | bool | None]
    def encode_content(self, encoding: Literal["gzip", "identity"] = "gzip", lazy: bool = False) -> None: ...
    def decode_content(self) -> None: ...
    def md5_etag(self, body: bytes | None = None, set_content_md5: bool = False) -> None: ...
    def __call__(self, environ: WSGIEnvironment, start_response: StartResponse) -> Iterable[bytes]: ...
    def conditional_response_app(self, environ: WSGIEnvironment, start_response: StartResponse) -> Iterable[bytes]: ...
    def app_iter_range(self, start: int, stop: int | None) -> AppIterRange: ...
    def __str__(self, skip_body: bool = False) -> str: ...

class ResponseBodyFile:
    mode: Literal["wb"]
    closed: Literal[False]
    response: Response
    def __init__(self, response: Response) -> None: ...
    @property
    def encoding(self) -> str | None: ...
    def write(self, text: str | bytes) -> int: ...
    def writelines(self, seq: Sequence[str | bytes]) -> int: ...
    def flush(self) -> None: ...
    def tell(self) -> int: ...

class AppIterRange:
    app_iter: Iterator[bytes]
    start: int
    stop: int | None
    def __init__(self, app_iter: Iterator[bytes], start: int, stop: int | None) -> None: ...
    def __iter__(self) -> Iterator[bytes]: ...
    def next(self) -> bytes: ...
    __next__ = next
    def close(self) -> None: ...

class EmptyResponse:
    def __init__(self, app_iter: Iterator[bytes] | None = None) -> None: ...
    def __iter__(self) -> Iterator[bytes]: ...
    def __len__(self) -> int: ...
    def next(self) -> bytes: ...
    __next__ = next
