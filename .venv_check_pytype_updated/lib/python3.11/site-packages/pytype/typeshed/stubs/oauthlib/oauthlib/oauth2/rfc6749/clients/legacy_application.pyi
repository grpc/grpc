from _typeshed import Incomplete

from .base import Client as Client

class LegacyApplicationClient(Client):
    grant_type: str
    def __init__(self, client_id, **kwargs) -> None: ...
    def prepare_request_body(
        self, username, password, body: str = "", scope: Incomplete | None = None, include_client_id: bool = False, **kwargs
    ): ...
