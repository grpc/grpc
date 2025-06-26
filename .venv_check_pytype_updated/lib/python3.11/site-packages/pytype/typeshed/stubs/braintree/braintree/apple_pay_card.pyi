from typing import Any

from braintree.resource import Resource as Resource

class ApplePayCard(Resource):
    class CardType:
        AmEx: str
        MasterCard: str
        Visa: str

    is_expired: Any
    subscriptions: Any
    def __init__(self, gateway, attributes) -> None: ...
    @property
    def expiration_date(self): ...
