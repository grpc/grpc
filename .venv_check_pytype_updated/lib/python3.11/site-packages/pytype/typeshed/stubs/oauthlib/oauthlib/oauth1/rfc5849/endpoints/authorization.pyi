from _typeshed import Incomplete

from .base import BaseEndpoint as BaseEndpoint

class AuthorizationEndpoint(BaseEndpoint):
    def create_verifier(self, request, credentials): ...
    def create_authorization_response(
        self,
        uri,
        http_method: str = "GET",
        body: Incomplete | None = None,
        headers: Incomplete | None = None,
        realms: Incomplete | None = None,
        credentials: Incomplete | None = None,
    ): ...
    def get_realms_and_credentials(
        self, uri, http_method: str = "GET", body: Incomplete | None = None, headers: Incomplete | None = None
    ): ...
