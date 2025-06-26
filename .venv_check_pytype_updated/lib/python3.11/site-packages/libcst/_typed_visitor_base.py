# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any, Callable, cast, TypeVar


# pyre-fixme[24]: Generic type `Callable` expects 2 type parameters.
F = TypeVar("F", bound=Callable)


def mark_no_op(f: F) -> F:
    """
    Annotates stubs with a field to indicate they should not be collected
    by BatchableCSTVisitor.get_visitors() to reduce function call
    overhead when running a batched visitor pass.
    """

    cast(Any, f)._is_no_op = True
    return f
