from _typeshed import Incomplete
from typing import Any

from braintree import version as version
from braintree.environment import Environment as Environment
from braintree.exceptions.authentication_error import AuthenticationError as AuthenticationError
from braintree.exceptions.authorization_error import AuthorizationError as AuthorizationError
from braintree.exceptions.gateway_timeout_error import GatewayTimeoutError as GatewayTimeoutError
from braintree.exceptions.http.connection_error import ConnectionError as ConnectionError
from braintree.exceptions.http.invalid_response_error import InvalidResponseError as InvalidResponseError
from braintree.exceptions.http.timeout_error import (
    ConnectTimeoutError as ConnectTimeoutError,
    ReadTimeoutError as ReadTimeoutError,
    TimeoutError as TimeoutError,
)
from braintree.exceptions.not_found_error import NotFoundError as NotFoundError
from braintree.exceptions.request_timeout_error import RequestTimeoutError as RequestTimeoutError
from braintree.exceptions.server_error import ServerError as ServerError
from braintree.exceptions.service_unavailable_error import ServiceUnavailableError as ServiceUnavailableError
from braintree.exceptions.too_many_requests_error import TooManyRequestsError as TooManyRequestsError
from braintree.exceptions.unexpected_error import UnexpectedError as UnexpectedError
from braintree.exceptions.upgrade_required_error import UpgradeRequiredError as UpgradeRequiredError
from braintree.util.xml_util import XmlUtil as XmlUtil

class Http:
    class ContentType:
        Xml: str
        Multipart: str
        Json: str

    @staticmethod
    def is_error_status(status): ...
    @staticmethod
    def raise_exception_from_status(status, message: Incomplete | None = None) -> None: ...
    config: Any
    environment: Any
    def __init__(self, config, environment: Incomplete | None = None) -> None: ...
    def post(self, path, params: Incomplete | None = None): ...
    def delete(self, path): ...
    def get(self, path): ...
    def put(self, path, params: Incomplete | None = None): ...
    def post_multipart(self, path, files, params: Incomplete | None = None): ...
    def http_do(self, http_verb, path, headers, request_body): ...
    def handle_exception(self, exception) -> None: ...
