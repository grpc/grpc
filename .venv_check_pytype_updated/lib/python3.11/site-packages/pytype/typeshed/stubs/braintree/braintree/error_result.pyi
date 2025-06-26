from typing import Any

from braintree.credit_card_verification import CreditCardVerification as CreditCardVerification
from braintree.errors import Errors as Errors

class ErrorResult:
    params: Any
    errors: Any
    message: Any
    credit_card_verification: Any
    transaction: Any
    subscription: Any
    merchant_account: Any
    def __init__(self, gateway, attributes) -> None: ...
    @property
    def is_success(self): ...
