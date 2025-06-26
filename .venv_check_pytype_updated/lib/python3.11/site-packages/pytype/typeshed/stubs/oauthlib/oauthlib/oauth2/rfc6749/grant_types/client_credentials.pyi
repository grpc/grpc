from typing import Any

from .base import GrantTypeBase as GrantTypeBase

log: Any

class ClientCredentialsGrant(GrantTypeBase):
    def create_token_response(self, request, token_handler): ...
    def validate_token_request(self, request) -> None: ...
