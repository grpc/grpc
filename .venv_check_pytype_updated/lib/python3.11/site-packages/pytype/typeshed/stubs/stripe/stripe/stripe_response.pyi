from typing import Any

class StripeResponseBase:
    code: Any
    headers: Any
    def __init__(self, code, headers) -> None: ...
    @property
    def idempotency_key(self): ...
    @property
    def request_id(self): ...

class StripeResponse(StripeResponseBase):
    body: Any
    data: Any
    def __init__(self, body, code, headers) -> None: ...

class StripeStreamResponse(StripeResponseBase):
    io: Any
    def __init__(self, io, code, headers) -> None: ...
