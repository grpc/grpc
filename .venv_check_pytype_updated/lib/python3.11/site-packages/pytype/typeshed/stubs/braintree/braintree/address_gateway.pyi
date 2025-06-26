from _typeshed import Incomplete
from typing import Any

from braintree.address import Address as Address
from braintree.error_result import ErrorResult as ErrorResult
from braintree.exceptions.not_found_error import NotFoundError as NotFoundError
from braintree.resource import Resource as Resource
from braintree.successful_result import SuccessfulResult as SuccessfulResult

class AddressGateway:
    gateway: Any
    config: Any
    def __init__(self, gateway) -> None: ...
    def create(self, params: Incomplete | None = None): ...
    def delete(self, customer_id, address_id): ...
    def find(self, customer_id, address_id): ...
    def update(self, customer_id, address_id, params: Incomplete | None = None): ...
