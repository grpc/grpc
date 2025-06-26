from typing import Any

from braintree.address import Address as Address
from braintree.resource import Resource as Resource

class MasterpassCard(Resource):
    billing_address: Any
    subscriptions: Any
    def __init__(self, gateway, attributes) -> None: ...
    @property
    def expiration_date(self): ...
    @property
    def masked_number(self): ...
