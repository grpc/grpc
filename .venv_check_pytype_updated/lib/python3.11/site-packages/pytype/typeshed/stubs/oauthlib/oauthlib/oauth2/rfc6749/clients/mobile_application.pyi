from _typeshed import Incomplete
from typing import Any

from .base import Client as Client

class MobileApplicationClient(Client):
    response_type: str
    def prepare_request_uri(
        self,
        uri,
        redirect_uri: Incomplete | None = None,
        scope: Incomplete | None = None,
        state: Incomplete | None = None,
        **kwargs,
    ): ...
    token: Any
    def parse_request_uri_response(self, uri, state: Incomplete | None = None, scope: Incomplete | None = None): ...
