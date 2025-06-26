# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import sys
from concurrent.futures import Executor, Future
from types import TracebackType
from typing import Callable, Optional, Type, TypeVar

if sys.version_info >= (3, 10):
    from typing import ParamSpec
else:
    from typing_extensions import ParamSpec

Return = TypeVar("Return")
Params = ParamSpec("Params")


class DummyExecutor(Executor):
    """
    Synchronous dummy `concurrent.futures.Executor` analogue.
    """

    def submit(
        self,
        fn: Callable[Params, Return],
        /,
        *args: Params.args,
        **kwargs: Params.kwargs,
    ) -> Future[Return]:
        future: Future[Return] = Future()
        try:
            result = fn(*args, **kwargs)
            future.set_result(result)
        except Exception as exc:
            future.set_exception(exc)
        return future

    def __enter__(self) -> "DummyExecutor":
        return self

    def __exit__(
        self,
        exc_type: Optional[Type[BaseException]],
        exc_val: Optional[BaseException],
        exc_tb: Optional[TracebackType],
    ) -> None:
        pass
