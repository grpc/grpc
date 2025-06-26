from typing import Any

from braintree.attribute_getter import AttributeGetter as AttributeGetter

class AuthorizationAdjustment(AttributeGetter):
    amount: Any
    def __init__(self, attributes) -> None: ...
