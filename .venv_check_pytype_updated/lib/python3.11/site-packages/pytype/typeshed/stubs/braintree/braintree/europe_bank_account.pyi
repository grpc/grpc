from braintree.configuration import Configuration as Configuration
from braintree.resource import Resource as Resource

class EuropeBankAccount(Resource):
    class MandateType:
        Business: str
        Consumer: str

    @staticmethod
    def signature(): ...
