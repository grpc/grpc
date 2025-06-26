from _typeshed import Incomplete
from typing import Any

from braintree.error_result import ErrorResult as ErrorResult
from braintree.exceptions.not_found_error import NotFoundError as NotFoundError
from braintree.merchant_account import MerchantAccount as MerchantAccount
from braintree.paginated_collection import PaginatedCollection as PaginatedCollection
from braintree.paginated_result import PaginatedResult as PaginatedResult
from braintree.resource import Resource as Resource
from braintree.resource_collection import ResourceCollection as ResourceCollection
from braintree.successful_result import SuccessfulResult as SuccessfulResult

class MerchantAccountGateway:
    gateway: Any
    config: Any
    def __init__(self, gateway) -> None: ...
    def create(self, params: Incomplete | None = None): ...
    def update(self, merchant_account_id, params: Incomplete | None = None): ...
    def find(self, merchant_account_id): ...
    def create_for_currency(self, params: Incomplete | None = None): ...
    def all(self): ...
