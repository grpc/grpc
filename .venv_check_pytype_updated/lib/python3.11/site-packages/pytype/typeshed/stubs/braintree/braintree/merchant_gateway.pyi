from typing import Any

from braintree.error_result import ErrorResult as ErrorResult
from braintree.exceptions.not_found_error import NotFoundError as NotFoundError
from braintree.merchant import Merchant as Merchant
from braintree.oauth_credentials import OAuthCredentials as OAuthCredentials
from braintree.resource import Resource as Resource
from braintree.resource_collection import ResourceCollection as ResourceCollection
from braintree.successful_result import SuccessfulResult as SuccessfulResult

class MerchantGateway:
    gateway: Any
    config: Any
    def __init__(self, gateway) -> None: ...
    def create(self, params): ...
