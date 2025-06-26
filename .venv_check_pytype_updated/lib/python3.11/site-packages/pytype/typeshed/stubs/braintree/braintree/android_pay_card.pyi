from typing import Any

from braintree.resource import Resource as Resource

class AndroidPayCard(Resource):
    is_expired: Any
    subscriptions: Any
    def __init__(self, gateway, attributes) -> None: ...
    @property
    def expiration_date(self): ...
    @property
    def last_4(self): ...
    @property
    def card_type(self): ...
