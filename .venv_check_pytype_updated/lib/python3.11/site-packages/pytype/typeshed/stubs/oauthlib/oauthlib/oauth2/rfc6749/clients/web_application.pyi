from _typeshed import Incomplete
from typing import Any

from .base import Client as Client

class WebApplicationClient(Client):
    grant_type: str
    code: Any
    def __init__(self, client_id, code: Incomplete | None = None, **kwargs) -> None: ...
    def prepare_request_uri(
        self,
        uri,
        redirect_uri: Incomplete | None = None,
        scope: Incomplete | None = None,
        state: Incomplete | None = None,
        code_challenge: str | None = None,
        code_challenge_method: str | None = "plain",
        **kwargs,
    ): ...
    def prepare_request_body(
        self,
        code: Incomplete | None = None,
        redirect_uri: Incomplete | None = None,
        body: str = "",
        include_client_id: bool = True,
        code_verifier: str | None = None,
        **kwargs,
    ): ...
    def parse_request_uri_response(self, uri, state: Incomplete | None = None): ...
