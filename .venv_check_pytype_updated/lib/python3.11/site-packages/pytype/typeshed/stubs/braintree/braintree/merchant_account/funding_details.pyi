from typing import Any

from braintree.attribute_getter import AttributeGetter as AttributeGetter

class FundingDetails(AttributeGetter):
    detail_list: Any
    def __init__(self, attributes) -> None: ...
