from braintree.attribute_getter import AttributeGetter as AttributeGetter
from braintree.configuration import Configuration as Configuration
from braintree.resource import Resource as Resource

class TransactionLineItem(AttributeGetter):
    class Kind:
        Credit: str
        Debit: str

    def __init__(self, attributes) -> None: ...
    @staticmethod
    def find_all(transaction_id): ...
