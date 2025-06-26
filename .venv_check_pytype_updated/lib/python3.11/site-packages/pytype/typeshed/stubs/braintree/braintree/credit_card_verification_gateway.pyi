from typing import Any

from braintree.credit_card_verification import CreditCardVerification as CreditCardVerification
from braintree.credit_card_verification_search import CreditCardVerificationSearch as CreditCardVerificationSearch
from braintree.error_result import ErrorResult as ErrorResult
from braintree.exceptions.not_found_error import NotFoundError as NotFoundError
from braintree.ids_search import IdsSearch as IdsSearch
from braintree.resource_collection import ResourceCollection as ResourceCollection
from braintree.successful_result import SuccessfulResult as SuccessfulResult

class CreditCardVerificationGateway:
    gateway: Any
    config: Any
    def __init__(self, gateway) -> None: ...
    def find(self, verification_id): ...
    def search(self, *query): ...
    def create(self, params): ...
