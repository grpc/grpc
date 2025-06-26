from _typeshed import Incomplete
from typing import Any, overload
from typing_extensions import TypeAlias

from stripe.stripe_object import StripeObject
from stripe.stripe_response import StripeResponse

def utf8(value): ...
def log_debug(message, **params) -> None: ...
def log_info(message, **params) -> None: ...
def dashboard_link(request_id): ...
def logfmt(props): ...

class class_method_variant:
    class_method_name: Any
    def __init__(self, class_method_name) -> None: ...
    method: Any
    def __call__(self, method): ...
    def __get__(self, obj, objtype: Incomplete | None = None): ...

@overload
def populate_headers(idempotency_key: None) -> None: ...
@overload
def populate_headers(idempotency_key: str) -> dict[str, str]: ...

_RespType: TypeAlias = dict[Any, Any] | StripeObject | StripeResponse

# undocumented
@overload
def convert_to_stripe_object(
    resp: list[Any],
    api_key: Incomplete | None = None,
    stripe_version: Incomplete | None = None,
    stripe_account: Incomplete | None = None,
) -> list[Any]: ...
@overload
def convert_to_stripe_object(
    resp: _RespType,
    api_key: Incomplete | None = None,
    stripe_version: Incomplete | None = None,
    stripe_account: Incomplete | None = None,
) -> StripeObject: ...
