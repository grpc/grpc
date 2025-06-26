from _typeshed import Incomplete
from typing import Any

from .base import BaseEndpoint as BaseEndpoint

log: Any

class ResourceEndpoint(BaseEndpoint):
    def __init__(self, default_token, token_types) -> None: ...
    @property
    def default_token(self): ...
    @property
    def default_token_type_handler(self): ...
    @property
    def tokens(self): ...
    def verify_request(
        self,
        uri,
        http_method: str = "GET",
        body: Incomplete | None = None,
        headers: Incomplete | None = None,
        scopes: Incomplete | None = None,
    ): ...
    def find_token_type(self, request): ...
