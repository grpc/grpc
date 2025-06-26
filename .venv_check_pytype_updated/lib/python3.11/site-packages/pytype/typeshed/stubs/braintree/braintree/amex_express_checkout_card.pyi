from typing import Any

from braintree.resource import Resource as Resource

class AmexExpressCheckoutCard(Resource):
    subscriptions: Any
    def __init__(self, gateway, attributes) -> None: ...
    @property
    def expiration_date(self): ...
