from typing import Any

from braintree.merchant_account import MerchantAccount as MerchantAccount
from braintree.resource import Resource as Resource

class Merchant(Resource):
    merchant_accounts: Any
    def __init__(self, gateway, attributes) -> None: ...
