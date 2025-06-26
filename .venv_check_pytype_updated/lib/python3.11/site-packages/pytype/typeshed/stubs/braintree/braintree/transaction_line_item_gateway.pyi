from typing import Any

from braintree.error_result import ErrorResult as ErrorResult
from braintree.exceptions.not_found_error import NotFoundError as NotFoundError
from braintree.exceptions.request_timeout_error import RequestTimeoutError as RequestTimeoutError
from braintree.resource import Resource as Resource
from braintree.resource_collection import ResourceCollection as ResourceCollection
from braintree.transaction_line_item import TransactionLineItem as TransactionLineItem

class TransactionLineItemGateway:
    gateway: Any
    config: Any
    def __init__(self, gateway) -> None: ...
    def find_all(self, transaction_id): ...
