from typing import Any

from braintree.discount import Discount as Discount
from braintree.resource_collection import ResourceCollection as ResourceCollection

class DiscountGateway:
    gateway: Any
    config: Any
    def __init__(self, gateway) -> None: ...
    def all(self): ...
