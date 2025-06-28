# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""A thread pool that logs exceptions raised by tasks executed within it."""

from concurrent import futures
import logging
from typing import Callable, Iterator, TypeVar

_LOGGER = logging.getLogger(__name__)

T = TypeVar('T')
ArgsT = TypeVar('ArgsT')
KwargsT = TypeVar('KwargsT')
IterableT = TypeVar('IterableT')


def _wrap(behavior: Callable[..., T]) -> Callable[..., T]:
    """Wraps an arbitrary callable behavior in exception-logging."""

    def _wrapping(*args: ArgsT, **kwargs: KwargsT) -> T:
        try:
            return behavior(*args, **kwargs)
        except Exception:
            _LOGGER.exception(
                "Unexpected exception from %s executed in logging pool!",
                behavior,
            )
            raise

    return _wrapping


class _LoggingPool(object):
    """An exception-logging futures.ThreadPoolExecutor-compatible thread pool."""

    def __init__(self, backing_pool: futures.ThreadPoolExecutor) -> None:
        self._backing_pool = backing_pool

    def __enter__(self) -> "_LoggingPool":
        return self

    def __exit__(self, exc_type: type | None, exc_val: BaseException | None, exc_tb: object | None) -> None:
        self._backing_pool.shutdown(wait=True)

    def submit(self, fn: Callable[..., T], *args: ArgsT, **kwargs: KwargsT) -> futures.Future[T]:
        return self._backing_pool.submit(_wrap(fn), *args, **kwargs)

    def map(self, func: Callable[..., T], *iterables: Iterator[IterableT], **kwargs: KwargsT) -> Iterator[T]:
        return self._backing_pool.map(
            _wrap(func), *iterables, timeout=kwargs.get("timeout", None)
        )

    def shutdown(self, wait: bool = True) -> None:
        self._backing_pool.shutdown(wait=wait)


def pool(max_workers: int) -> _LoggingPool:
    """Creates a thread pool that logs exceptions raised by the tasks within it.

    Args:
      max_workers: The maximum number of worker threads to allow the pool.

    Returns:
      A futures.ThreadPoolExecutor-compatible thread pool that logs exceptions
        raised by the tasks executed within it.
    """
    return _LoggingPool(futures.ThreadPoolExecutor(max_workers))
