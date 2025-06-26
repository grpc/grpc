from _typeshed import Incomplete
from typing import Any

from braintree.exceptions.authentication_error import AuthenticationError as AuthenticationError
from braintree.exceptions.authorization_error import AuthorizationError as AuthorizationError
from braintree.exceptions.not_found_error import NotFoundError as NotFoundError
from braintree.exceptions.server_error import ServerError as ServerError
from braintree.exceptions.service_unavailable_error import ServiceUnavailableError as ServiceUnavailableError
from braintree.exceptions.too_many_requests_error import TooManyRequestsError as TooManyRequestsError
from braintree.exceptions.unexpected_error import UnexpectedError as UnexpectedError
from braintree.exceptions.upgrade_required_error import UpgradeRequiredError as UpgradeRequiredError
from braintree.util.http import Http as Http

class GraphQLClient(Http):
    @staticmethod
    def raise_exception_for_graphql_error(response) -> None: ...
    graphql_headers: Any
    def __init__(self, config: Incomplete | None = None, environment: Incomplete | None = None) -> None: ...
    def query(self, definition, variables: Incomplete | None = None, operation_name: Incomplete | None = None): ...
