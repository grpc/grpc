# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

"""
Used by visitors. This is hoisted into a separate module to avoid some circular
dependencies in the definition of CSTNode.
"""

from enum import auto, Enum


class RemovalSentinel(Enum):
    """
    A :attr:`RemovalSentinel.REMOVE` value should be returned by a
    :meth:`CSTTransformer.on_leave` method when we want to remove that child from its
    parent. As a convenience, this can be constructed by calling
    :func:`libcst.RemoveFromParent`.

    The parent node should make a best-effort to remove the child, but may raise an
    exception when removing the child doesn't make sense, or could change the semantics
    in an unexpected way. For example, a function definition with no name doesn't make
    sense, but removing one of the arguments is valid.

    In we can't automatically remove the child, the developer should instead remove the
    child by constructing a new parent in the parent's :meth:`~CSTTransformer.on_leave`
    call.

    We use this instead of ``None`` to force developers to be explicit about deletions.
    Because ``None`` is the default return value for a function with no return
    statement, it would be too easy to accidentally delete nodes from the tree by
    forgetting to return a value.
    """

    REMOVE = auto()


def RemoveFromParent() -> RemovalSentinel:
    """
    A convenience method for requesting that this node be removed by its parent.
    Use this in place of returning :class:`RemovalSentinel` directly.
    For example, to remove all arguments unconditionally::

        def leave_Arg(
            self, original_node: cst.Arg, updated_node: cst.Arg
        ) -> Union[cst.Arg, cst.RemovalSentinel]:
            return RemoveFromParent()
    """
    return RemovalSentinel.REMOVE
