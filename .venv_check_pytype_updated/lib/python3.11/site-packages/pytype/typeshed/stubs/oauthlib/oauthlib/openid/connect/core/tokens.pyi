from _typeshed import Incomplete
from typing import Any

from oauthlib.oauth2.rfc6749.tokens import TokenBase as TokenBase

class JWTToken(TokenBase):
    request_validator: Any
    token_generator: Any
    refresh_token_generator: Any
    expires_in: Any
    def __init__(
        self,
        request_validator: Incomplete | None = None,
        token_generator: Incomplete | None = None,
        expires_in: Incomplete | None = None,
        refresh_token_generator: Incomplete | None = None,
    ) -> None: ...
    def create_token(self, request, refresh_token: bool = False): ...
    def validate_request(self, request): ...
    def estimate_type(self, request): ...
