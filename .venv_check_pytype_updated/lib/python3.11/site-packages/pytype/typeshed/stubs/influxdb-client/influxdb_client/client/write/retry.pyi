from _typeshed import Incomplete
from collections.abc import Callable

from urllib3 import Retry

logger: Incomplete

class WritesRetry(Retry):
    jitter_interval: Incomplete
    total: Incomplete
    retry_interval: Incomplete
    max_retry_delay: Incomplete
    max_retry_time: Incomplete
    exponential_base: Incomplete
    retry_timeout: Incomplete
    retry_callback: Incomplete
    def __init__(
        self,
        jitter_interval: int = 0,
        max_retry_delay: int = 125,
        exponential_base: int = 2,
        max_retry_time: int = 180,
        total: int = 5,
        retry_interval: int = 5,
        retry_callback: Callable[[Exception], int] | None = None,
        **kw,
    ) -> None: ...
    def new(self, **kw): ...
    def is_retry(self, method, status_code, has_retry_after: bool = False): ...
    def get_backoff_time(self): ...
    def get_retry_after(self, response): ...
    def increment(
        self,
        method: Incomplete | None = None,
        url: Incomplete | None = None,
        response: Incomplete | None = None,
        error: Incomplete | None = None,
        _pool: Incomplete | None = None,
        _stacktrace: Incomplete | None = None,
    ): ...
