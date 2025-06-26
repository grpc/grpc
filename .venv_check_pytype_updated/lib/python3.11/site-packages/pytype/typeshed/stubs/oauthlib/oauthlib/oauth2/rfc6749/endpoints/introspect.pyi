from _typeshed import Incomplete
from typing import Any

from .base import BaseEndpoint as BaseEndpoint

log: Any

class IntrospectEndpoint(BaseEndpoint):
    valid_token_types: Any
    valid_request_methods: Any
    request_validator: Any
    supported_token_types: Any
    def __init__(self, request_validator, supported_token_types: Incomplete | None = None) -> None: ...
    def create_introspect_response(
        self, uri, http_method: str = "POST", body: Incomplete | None = None, headers: Incomplete | None = None
    ): ...
    def validate_introspect_request(self, request) -> None: ...
