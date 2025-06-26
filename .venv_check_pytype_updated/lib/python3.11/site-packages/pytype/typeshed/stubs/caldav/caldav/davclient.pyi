from _typeshed import Incomplete
from collections.abc import Iterable, Mapping
from types import TracebackType
from typing_extensions import Self, TypeAlias
from urllib.parse import ParseResult, SplitResult

from requests.auth import AuthBase
from requests.models import Response
from requests.sessions import _Timeout
from requests.structures import CaseInsensitiveDict

from .lib.url import URL
from .objects import Calendar, DAVObject, Principal

_Element: TypeAlias = Incomplete  # actually lxml.etree._Element

class DAVResponse:
    reason: str
    tree: _Element | None
    status: int
    headers: CaseInsensitiveDict[str]
    objects: dict[str, dict[str, str]]  # only defined after call to find_objects_and_props()
    huge_tree: bool
    def __init__(self, response: Response, davclient: DAVClient | None = None) -> None: ...
    @property
    def raw(self) -> str: ...
    def validate_status(self, status: str) -> None: ...
    def find_objects_and_props(self) -> None: ...
    def expand_simple_props(
        self, props: Iterable[Incomplete] = [], multi_value_props: Iterable[Incomplete] = [], xpath: str | None = None
    ) -> dict[str, dict[str, str]]: ...

class DAVClient:
    proxy: str | None
    url: URL
    headers: dict[str, str]
    username: str | None
    password: str | None
    auth: AuthBase | None
    timeout: _Timeout | None
    ssl_verify_cert: bool | str
    ssl_cert: str | tuple[str, str] | None
    huge_tree: bool
    def __init__(
        self,
        url: str,
        proxy: str | None = None,
        username: str | None = None,
        password: str | None = None,
        auth: AuthBase | None = None,
        timeout: _Timeout | None = None,
        ssl_verify_cert: bool | str = True,
        ssl_cert: str | tuple[str, str] | None = None,
        headers: dict[str, str] = {},
        huge_tree: bool = False,
    ) -> None: ...
    def __enter__(self) -> Self: ...
    def __exit__(
        self, exc_type: type[BaseException] | None, exc_value: BaseException | None, traceback: TracebackType | None
    ) -> None: ...
    def principal(self, *, url: str | ParseResult | SplitResult | URL | None = ...) -> Principal: ...
    def calendar(
        self,
        url: str | ParseResult | SplitResult | URL | None = ...,
        parent: DAVObject | None = ...,
        name: str | None = ...,
        id: str | None = ...,
        props: Mapping[Incomplete, Incomplete] = ...,
        **extra: Incomplete,
    ) -> Calendar: ...
    def check_dav_support(self) -> str | None: ...
    def check_cdav_support(self) -> bool: ...
    def check_scheduling_support(self) -> bool: ...
    def propfind(self, url: str | None = None, props: str = "", depth: int = 0) -> DAVResponse: ...
    def proppatch(self, url: str, body: str, dummy: None = None) -> DAVResponse: ...
    def report(self, url: str, query: str = "", depth: int = 0) -> DAVResponse: ...
    def mkcol(self, url: str, body: str, dummy: None = None) -> DAVResponse: ...
    def mkcalendar(self, url: str, body: str = "", dummy: None = None) -> DAVResponse: ...
    def put(self, url: str, body: str, headers: Mapping[str, str] = {}) -> DAVResponse: ...
    def post(self, url: str, body: str, headers: Mapping[str, str] = {}) -> DAVResponse: ...
    def delete(self, url: str) -> DAVResponse: ...
    def options(self, url: str) -> DAVResponse: ...
    def request(self, url: str, method: str = "GET", body: str = "", headers: Mapping[str, str] = {}) -> DAVResponse: ...
