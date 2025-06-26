from _typeshed import Incomplete
from typing import Any

from braintree.error_result import ErrorResult as ErrorResult
from braintree.exceptions.not_found_error import NotFoundError as NotFoundError
from braintree.paypal_account import PayPalAccount as PayPalAccount
from braintree.resource import Resource as Resource
from braintree.successful_result import SuccessfulResult as SuccessfulResult

class PayPalAccountGateway:
    gateway: Any
    config: Any
    def __init__(self, gateway) -> None: ...
    def find(self, paypal_account_token): ...
    def delete(self, paypal_account_token): ...
    def update(self, paypal_account_token, params: Incomplete | None = None): ...
