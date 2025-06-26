from typing import Any

from braintree.util.crypto import Crypto as Crypto

class SignatureService:
    private_key: Any
    hmac_hash: Any
    def __init__(self, private_key, hashfunc=...) -> None: ...
    def sign(self, data): ...
    def hash(self, data): ...
