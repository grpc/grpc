from _typeshed import Incomplete

from braintree import exceptions as exceptions
from braintree.configuration import Configuration as Configuration
from braintree.signature_service import SignatureService as SignatureService
from braintree.util.crypto import Crypto as Crypto

class ClientToken:
    @staticmethod
    def generate(params: Incomplete | None = None, gateway: Incomplete | None = None): ...
    @staticmethod
    def generate_signature(): ...
