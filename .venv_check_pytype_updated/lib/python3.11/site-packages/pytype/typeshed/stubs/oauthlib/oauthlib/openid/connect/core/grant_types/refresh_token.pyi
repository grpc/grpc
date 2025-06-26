from _typeshed import Incomplete

from .base import GrantTypeBase

log: Incomplete

class RefreshTokenGrant(GrantTypeBase):
    proxy_target: Incomplete
    def __init__(self, request_validator: Incomplete | None = None, **kwargs) -> None: ...
    def add_id_token(self, token, token_handler, request): ...
