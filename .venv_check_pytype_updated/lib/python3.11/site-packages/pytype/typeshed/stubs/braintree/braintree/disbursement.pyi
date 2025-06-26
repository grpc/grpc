from typing import Any

from braintree.merchant_account import MerchantAccount as MerchantAccount
from braintree.resource import Resource as Resource
from braintree.transaction_search import TransactionSearch as TransactionSearch

class Disbursement(Resource):
    class Type:
        Credit: str
        Debit: str

    amount: Any
    merchant_account: Any
    def __init__(self, gateway, attributes) -> None: ...
    def transactions(self): ...
    def is_credit(self): ...
    def is_debit(self): ...
