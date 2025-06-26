from _typeshed import Incomplete
from typing import Any

from .base import GrantTypeBase as GrantTypeBase

log: Any

class RefreshTokenGrant(GrantTypeBase):
    def __init__(self, request_validator: Incomplete | None = None, issue_new_refresh_tokens: bool = True, **kwargs) -> None: ...
    def create_token_response(self, request, token_handler): ...
    def validate_token_request(self, request) -> None: ...
