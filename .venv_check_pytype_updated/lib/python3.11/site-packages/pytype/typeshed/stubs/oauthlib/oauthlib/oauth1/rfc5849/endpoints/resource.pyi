from _typeshed import Incomplete
from typing import Any

from .base import BaseEndpoint as BaseEndpoint

log: Any

class ResourceEndpoint(BaseEndpoint):
    def validate_protected_resource_request(
        self,
        uri,
        http_method: str = "GET",
        body: Incomplete | None = None,
        headers: Incomplete | None = None,
        realms: Incomplete | None = None,
    ): ...
