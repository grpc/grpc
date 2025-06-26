from typing import Any

from braintree.attribute_getter import AttributeGetter as AttributeGetter

class AddressDetails(AttributeGetter):
    detail_list: Any
    def __init__(self, attributes) -> None: ...
