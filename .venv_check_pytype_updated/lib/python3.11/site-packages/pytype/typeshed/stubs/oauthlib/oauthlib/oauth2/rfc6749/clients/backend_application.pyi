from _typeshed import Incomplete

from .base import Client as Client

class BackendApplicationClient(Client):
    grant_type: str
    def prepare_request_body(
        self, body: str = "", scope: Incomplete | None = None, include_client_id: bool = False, **kwargs
    ): ...
