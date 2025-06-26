from typing import Any

from braintree.payment_method_parser import parse_payment_method as parse_payment_method
from braintree.resource import Resource as Resource

class RevokedPaymentMethodMetadata(Resource):
    revoked_payment_method: Any
    customer_id: Any
    token: Any
    def __init__(self, gateway, attributes) -> None: ...
