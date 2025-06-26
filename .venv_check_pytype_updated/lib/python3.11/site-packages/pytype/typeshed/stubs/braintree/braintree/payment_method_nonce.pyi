from typing import Any

from braintree.bin_data import BinData as BinData
from braintree.configuration import Configuration as Configuration
from braintree.resource import Resource as Resource
from braintree.three_d_secure_info import ThreeDSecureInfo as ThreeDSecureInfo

class PaymentMethodNonce(Resource):
    @staticmethod
    def create(payment_method_token, params={}): ...
    @staticmethod
    def find(payment_method_nonce): ...
    three_d_secure_info: Any
    authentication_insight: Any
    bin_data: Any
    def __init__(self, gateway, attributes) -> None: ...
