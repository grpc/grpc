from typing import Any

from braintree.error_result import ErrorResult as ErrorResult
from braintree.exceptions.not_found_error import NotFoundError as NotFoundError
from braintree.plan import Plan as Plan
from braintree.resource import Resource as Resource
from braintree.resource_collection import ResourceCollection as ResourceCollection
from braintree.successful_result import SuccessfulResult as SuccessfulResult

class PlanGateway:
    gateway: Any
    config: Any
    def __init__(self, gateway) -> None: ...
    def all(self): ...
