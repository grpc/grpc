from typing import Any

from braintree.resource import Resource as Resource

class GrantedPaymentInstrumentUpdate(Resource):
    payment_method_nonce: Any
    def __init__(self, gateway, attributes) -> None: ...
