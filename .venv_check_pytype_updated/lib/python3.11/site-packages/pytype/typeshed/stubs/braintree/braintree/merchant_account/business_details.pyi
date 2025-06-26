from typing import Any

from braintree.attribute_getter import AttributeGetter as AttributeGetter
from braintree.merchant_account.address_details import AddressDetails as AddressDetails

class BusinessDetails(AttributeGetter):
    detail_list: Any
    address_details: Any
    def __init__(self, attributes) -> None: ...
