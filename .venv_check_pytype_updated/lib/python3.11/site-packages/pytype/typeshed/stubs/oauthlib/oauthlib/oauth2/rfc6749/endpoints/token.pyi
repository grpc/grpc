from _typeshed import Incomplete
from typing import Any

from .base import BaseEndpoint as BaseEndpoint

log: Any

class TokenEndpoint(BaseEndpoint):
    valid_request_methods: Any
    def __init__(self, default_grant_type, default_token_type, grant_types) -> None: ...
    @property
    def grant_types(self): ...
    @property
    def default_grant_type(self): ...
    @property
    def default_grant_type_handler(self): ...
    @property
    def default_token_type(self): ...
    def create_token_response(
        self,
        uri,
        http_method: str = "POST",
        body: Incomplete | None = None,
        headers: Incomplete | None = None,
        credentials: Incomplete | None = None,
        grant_type_for_scope: Incomplete | None = None,
        claims: Incomplete | None = None,
    ): ...
    def validate_token_request(self, request) -> None: ...
