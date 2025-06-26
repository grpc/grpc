from typing import Any

from braintree.resource import Resource as Resource

class SubscriptionStatusEvent(Resource):
    balance: Any
    price: Any
    def __init__(self, gateway, attributes) -> None: ...
