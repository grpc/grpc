from _typeshed import Incomplete
from typing import Any

from braintree import exceptions as exceptions
from braintree.client_token import ClientToken as ClientToken
from braintree.resource import Resource as Resource

class ClientTokenGateway:
    gateway: Any
    config: Any
    def __init__(self, gateway) -> None: ...
    def generate(self, params: Incomplete | None = None): ...
