from typing import Any

from braintree.add_on import AddOn as AddOn
from braintree.resource_collection import ResourceCollection as ResourceCollection

class AddOnGateway:
    gateway: Any
    config: Any
    def __init__(self, gateway) -> None: ...
    def all(self): ...
