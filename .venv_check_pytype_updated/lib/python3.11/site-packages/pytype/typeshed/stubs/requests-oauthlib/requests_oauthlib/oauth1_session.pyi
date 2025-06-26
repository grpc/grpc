from _typeshed import Incomplete
from logging import Logger
from typing import TypedDict
from typing_extensions import TypeAlias

import requests
from oauthlib.oauth1 import Client

from . import OAuth1

# should be dict[str, str] but could look different
_ParsedToken: TypeAlias = dict[str, Incomplete]

class _TokenDict(TypedDict, total=False):
    oauth_token: Incomplete  # oauthlib.oauth1.Client.resource_owner_key
    oauth_token_secret: Incomplete  # oauthlib.oauth1.Client.resource_token_secret
    oauth_verifier: Incomplete  # oauthlib.oauth1.Client.oauth_verifier

log: Logger

def urldecode(body): ...

class TokenRequestDenied(ValueError):
    response: requests.Response
    def __init__(self, message: str, response: requests.Response) -> None: ...
    @property
    def status_code(self) -> int: ...

class TokenMissing(ValueError):
    response: requests.Response
    def __init__(self, message: str, response: requests.Response) -> None: ...

class VerifierMissing(ValueError): ...

class OAuth1Session(requests.Session):
    auth: OAuth1
    def __init__(
        self,
        client_key,
        client_secret: Incomplete | None = None,
        resource_owner_key: Incomplete | None = None,
        resource_owner_secret: Incomplete | None = None,
        callback_uri: Incomplete | None = None,
        signature_method="HMAC-SHA1",
        signature_type="AUTH_HEADER",
        rsa_key: Incomplete | None = None,
        verifier: Incomplete | None = None,
        client_class: type[Client] | None = None,
        force_include_body: bool = False,
        *,
        encoding: str = "utf-8",
        nonce: Incomplete | None = None,
        timestamp: Incomplete | None = None,
    ) -> None: ...
    @property
    def token(self) -> _TokenDict: ...
    @token.setter
    def token(self, value: _TokenDict) -> None: ...
    @property
    def authorized(self) -> bool: ...
    def authorization_url(self, url: str, request_token: Incomplete | None = None, **kwargs) -> str: ...
    def fetch_request_token(self, url: str, realm: Incomplete | None = None, **request_kwargs) -> _ParsedToken: ...
    def fetch_access_token(self, url: str, verifier: Incomplete | None = None, **request_kwargs) -> _ParsedToken: ...
    def parse_authorization_response(self, url: str) -> _ParsedToken: ...
    def rebuild_auth(self, prepared_request: requests.PreparedRequest, response: requests.Response) -> None: ...
