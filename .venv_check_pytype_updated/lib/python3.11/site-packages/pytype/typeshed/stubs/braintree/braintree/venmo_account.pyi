from typing import Any

from braintree.resource import Resource as Resource

class VenmoAccount(Resource):
    subscriptions: Any
    def __init__(self, gateway, attributes) -> None: ...
