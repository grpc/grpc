from typing import Any

from braintree.attribute_getter import AttributeGetter as AttributeGetter

class TransactionDetails(AttributeGetter):
    amount: Any
    def __init__(self, attributes) -> None: ...
