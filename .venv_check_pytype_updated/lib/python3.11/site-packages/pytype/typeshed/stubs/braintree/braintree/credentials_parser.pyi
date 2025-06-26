from _typeshed import Incomplete
from typing import Any

from braintree.environment import Environment as Environment
from braintree.exceptions.configuration_error import ConfigurationError as ConfigurationError

class CredentialsParser:
    client_id: Any
    client_secret: Any
    access_token: Any
    def __init__(
        self, client_id: Incomplete | None = None, client_secret: Incomplete | None = None, access_token: Incomplete | None = None
    ) -> None: ...
    environment: Any
    def parse_client_credentials(self) -> None: ...
    merchant_id: Any
    def parse_access_token(self) -> None: ...
    def get_environment(self, credential): ...
    def get_merchant_id(self, credential): ...
