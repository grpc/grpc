from _typeshed import Incomplete
from logging import Logger
from typing import Any, Literal, Protocol, TypedDict, overload
from typing_extensions import TypeAlias

import requests
from oauthlib.oauth2 import Client
from requests.cookies import RequestsCookieJar

_Token: TypeAlias = dict[str, Incomplete]  # oauthlib.oauth2.Client.token

class _AccessTokenResponseHook(Protocol):
    def __call__(self, response: requests.Response, /) -> requests.Response: ...

class _RefreshTokenResponseHook(Protocol):
    def __call__(self, response: requests.Response, /) -> requests.Response: ...

class _ProtectedRequestHook(Protocol):
    def __call__(
        self, url: Incomplete, headers: Incomplete, data: Incomplete, /
    ) -> tuple[Incomplete, Incomplete, Incomplete]: ...

class _ComplianceHooks(TypedDict):
    access_token_response: set[_AccessTokenResponseHook]
    refresh_token_response: set[_RefreshTokenResponseHook]
    protected_request: set[_ProtectedRequestHook]

log: Logger

class TokenUpdated(Warning):
    token: Incomplete
    def __init__(self, token) -> None: ...

class OAuth2Session(requests.Session):
    redirect_uri: Incomplete
    state: Incomplete
    auto_refresh_url: str | None
    auto_refresh_kwargs: dict[str, Any]
    token_updater: Incomplete
    compliance_hook: _ComplianceHooks
    scope: Incomplete | None
    def __init__(
        self,
        client_id: Incomplete | None = None,
        client: Client | None = None,
        auto_refresh_url: str | None = None,
        auto_refresh_kwargs: dict[str, Any] | None = None,
        scope: Incomplete | None = None,
        redirect_uri: Incomplete | None = None,
        token: Incomplete | None = None,
        state: Incomplete | None = None,
        token_updater: Incomplete | None = None,
        **kwargs,
    ) -> None: ...
    def new_state(self): ...
    @property
    def client_id(self) -> Incomplete | None: ...  # oauthlib.oauth2.Client.client_id
    @client_id.setter
    def client_id(self, value: Incomplete | None) -> None: ...
    @client_id.deleter
    def client_id(self) -> None: ...
    @property
    def token(self): ...  # oauthlib.oauth2.Client.token
    @token.setter
    def token(self, value) -> None: ...
    @property
    def access_token(self): ...  # oauthlib.oauth2.Client.access_token
    @access_token.setter
    def access_token(self, value) -> None: ...
    @access_token.deleter
    def access_token(self) -> None: ...
    @property
    def authorized(self) -> bool: ...
    def authorization_url(self, url: str, state: Incomplete | None = None, **kwargs) -> tuple[str, str]: ...
    def fetch_token(
        self,
        token_url: str,
        code: Incomplete | None = None,
        authorization_response: Incomplete | None = None,
        body: str = "",
        auth: Incomplete | None = None,
        username: Incomplete | None = None,
        password: Incomplete | None = None,
        method: str = "POST",
        force_querystring: bool = False,
        timeout: Incomplete | None = None,
        headers: Incomplete | None = None,
        verify: bool = True,
        proxies: Incomplete | None = None,
        include_client_id: Incomplete | None = None,
        client_secret: Incomplete | None = None,
        cert: Incomplete | None = None,
        **kwargs,
    ) -> _Token: ...
    def token_from_fragment(self, authorization_response: str) -> _Token: ...
    def refresh_token(
        self,
        token_url: str,
        refresh_token: Incomplete | None = None,
        body: str = "",
        auth: Incomplete | None = None,
        timeout: Incomplete | None = None,
        headers: Incomplete | None = None,
        verify: bool = True,
        proxies: Incomplete | None = None,
        **kwargs,
    ) -> _Token: ...
    def request(  # type: ignore[override]
        self,
        method: str | bytes,
        url: str | bytes,
        data: requests.sessions._Data | None = None,
        headers: requests.sessions._HeadersUpdateMapping | None = None,
        withhold_token: bool = False,
        client_id: Incomplete | None = None,
        client_secret: Incomplete | None = None,
        *,
        params: requests.sessions._Params | None = None,
        cookies: None | RequestsCookieJar | requests.sessions._TextMapping = None,
        files: requests.sessions._Files | None = None,
        auth: requests.sessions._Auth | None = None,
        timeout: requests.sessions._Timeout | None = None,
        allow_redirects: bool = True,
        proxies: requests.sessions._TextMapping | None = None,
        hooks: requests.sessions._HooksInput | None = None,
        stream: bool | None = None,
        verify: requests.sessions._Verify | None = None,
        cert: requests.sessions._Cert | None = None,
        json: Incomplete | None = None,
    ) -> requests.Response: ...
    @overload
    def register_compliance_hook(self, hook_type: Literal["access_token_response"], hook: _AccessTokenResponseHook) -> None: ...
    @overload
    def register_compliance_hook(self, hook_type: Literal["refresh_token_response"], hook: _RefreshTokenResponseHook) -> None: ...
    @overload
    def register_compliance_hook(self, hook_type: Literal["protected_request"], hook: _ProtectedRequestHook) -> None: ...
