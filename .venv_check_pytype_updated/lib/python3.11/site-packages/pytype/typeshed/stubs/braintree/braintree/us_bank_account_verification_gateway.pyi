from typing import Any

from braintree.error_result import ErrorResult as ErrorResult
from braintree.exceptions.not_found_error import NotFoundError as NotFoundError
from braintree.resource_collection import ResourceCollection as ResourceCollection
from braintree.successful_result import SuccessfulResult as SuccessfulResult
from braintree.us_bank_account_verification import UsBankAccountVerification as UsBankAccountVerification
from braintree.us_bank_account_verification_search import UsBankAccountVerificationSearch as UsBankAccountVerificationSearch

class UsBankAccountVerificationGateway:
    gateway: Any
    config: Any
    def __init__(self, gateway) -> None: ...
    def confirm_micro_transfer_amounts(self, verification_id, amounts): ...
    def find(self, verification_id): ...
    def search(self, *query): ...
