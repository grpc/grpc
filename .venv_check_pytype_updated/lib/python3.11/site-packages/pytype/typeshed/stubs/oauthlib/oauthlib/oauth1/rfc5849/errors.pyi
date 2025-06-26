from _typeshed import Incomplete
from typing import Any

class OAuth1Error(Exception):
    error: Any
    description: str
    uri: Any
    status_code: Any
    def __init__(
        self,
        description: Incomplete | None = None,
        uri: Incomplete | None = None,
        status_code: int = 400,
        request: Incomplete | None = None,
    ) -> None: ...
    def in_uri(self, uri): ...
    @property
    def twotuples(self): ...
    @property
    def urlencoded(self): ...

class InsecureTransportError(OAuth1Error):
    error: str
    description: str

class InvalidSignatureMethodError(OAuth1Error):
    error: str

class InvalidRequestError(OAuth1Error):
    error: str

class InvalidClientError(OAuth1Error):
    error: str
