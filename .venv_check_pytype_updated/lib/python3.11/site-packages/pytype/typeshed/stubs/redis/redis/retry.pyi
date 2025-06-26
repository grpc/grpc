from collections.abc import Callable, Iterable
from typing import TypeVar

from redis.backoff import AbstractBackoff

_T = TypeVar("_T")

class Retry:
    def __init__(self, backoff: AbstractBackoff, retries: int, supported_errors: tuple[type[Exception], ...] = ...) -> None: ...
    def update_supported_errors(self, specified_errors: Iterable[type[Exception]]) -> None: ...
    def call_with_retry(self, do: Callable[[], _T], fail: Callable[[Exception], object]) -> _T: ...
