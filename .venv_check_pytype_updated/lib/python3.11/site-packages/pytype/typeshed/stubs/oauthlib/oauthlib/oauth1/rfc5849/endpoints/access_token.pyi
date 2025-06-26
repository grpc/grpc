from _typeshed import Incomplete
from typing import Any

from .base import BaseEndpoint as BaseEndpoint

log: Any

class AccessTokenEndpoint(BaseEndpoint):
    def create_access_token(self, request, credentials): ...
    def create_access_token_response(
        self,
        uri,
        http_method: str = "GET",
        body: Incomplete | None = None,
        headers: Incomplete | None = None,
        credentials: Incomplete | None = None,
    ): ...
    def validate_access_token_request(self, request): ...
