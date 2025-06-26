from _typeshed import Incomplete
from typing import Any

class BaseEndpoint:
    request_validator: Any
    token_generator: Any
    def __init__(self, request_validator, token_generator: Incomplete | None = None) -> None: ...
