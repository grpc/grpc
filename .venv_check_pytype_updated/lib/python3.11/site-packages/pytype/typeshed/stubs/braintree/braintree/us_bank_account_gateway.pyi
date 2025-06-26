from typing import Any

from braintree.exceptions.not_found_error import NotFoundError as NotFoundError
from braintree.us_bank_account import UsBankAccount as UsBankAccount

class UsBankAccountGateway:
    gateway: Any
    config: Any
    def __init__(self, gateway) -> None: ...
    def find(self, us_bank_account_token): ...
