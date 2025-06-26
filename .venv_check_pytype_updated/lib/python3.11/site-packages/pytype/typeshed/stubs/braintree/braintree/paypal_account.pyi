from _typeshed import Incomplete
from typing import Any

from braintree.configuration import Configuration as Configuration
from braintree.resource import Resource as Resource

class PayPalAccount(Resource):
    @staticmethod
    def find(paypal_account_token): ...
    @staticmethod
    def delete(paypal_account_token): ...
    @staticmethod
    def update(paypal_account_token, params: Incomplete | None = None): ...
    @staticmethod
    def signature(): ...
    subscriptions: Any
    def __init__(self, gateway, attributes) -> None: ...
