from _typeshed import Incomplete
from typing import Any

from stripe import error as error, http_client as http_client, oauth_error as oauth_error, version as version
from stripe.multipart_data_generator import MultipartDataGenerator as MultipartDataGenerator
from stripe.stripe_response import StripeResponse as StripeResponse, StripeStreamResponse as StripeStreamResponse

class APIRequestor:
    api_base: Any
    api_key: Any
    api_version: Any
    stripe_account: Any
    def __init__(
        self,
        key: Incomplete | None = None,
        client: Incomplete | None = None,
        api_base: Incomplete | None = None,
        api_version: Incomplete | None = None,
        account: Incomplete | None = None,
    ) -> None: ...
    @classmethod
    def format_app_info(cls, info): ...
    def request(self, method, url, params: Incomplete | None = None, headers: Incomplete | None = None): ...
    def request_stream(self, method, url, params: Incomplete | None = None, headers: Incomplete | None = None): ...
    def handle_error_response(self, rbody, rcode, resp, rheaders) -> None: ...
    def specific_api_error(self, rbody, rcode, resp, rheaders, error_data): ...
    def specific_oauth_error(self, rbody, rcode, resp, rheaders, error_code): ...
    def request_headers(self, api_key, method): ...
    def request_raw(
        self,
        method,
        url,
        params: Incomplete | None = None,
        supplied_headers: Incomplete | None = None,
        is_streaming: bool = False,
    ): ...
    def interpret_response(self, rbody, rcode, rheaders): ...
    def interpret_streaming_response(self, stream, rcode, rheaders): ...
