from typing import Any

from braintree.attribute_getter import AttributeGetter as AttributeGetter

class DisbursementDetail(AttributeGetter):
    settlement_amount: Any
    settlement_currency_exchange_rate: Any
    def __init__(self, attributes) -> None: ...
    @property
    def is_valid(self): ...
