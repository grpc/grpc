from typing import Any

from braintree.address import Address as Address
from braintree.credit_card_verification import CreditCardVerification as CreditCardVerification
from braintree.resource import Resource as Resource

class VisaCheckoutCard(Resource):
    billing_address: Any
    subscriptions: Any
    verification: Any
    def __init__(self, gateway, attributes): ...
    @property
    def expiration_date(self): ...
    @property
    def masked_number(self): ...
