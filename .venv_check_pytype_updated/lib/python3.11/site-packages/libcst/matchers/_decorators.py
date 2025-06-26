# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Callable, TypeVar

from libcst.matchers._matcher_base import BaseMatcherNode

_CSTVisitFuncT = TypeVar("_CSTVisitFuncT")


VISIT_POSITIVE_MATCHER_ATTR: str = "_call_if_inside_matcher"
VISIT_NEGATIVE_MATCHER_ATTR: str = "_call_if_not_inside_matcher"
CONSTRUCTED_VISIT_MATCHER_ATTR: str = "_visit_matcher"
CONSTRUCTED_LEAVE_MATCHER_ATTR: str = "_leave_matcher"


def call_if_inside(
    matcher: BaseMatcherNode,
    # pyre-fixme[34]: `Variable[_CSTVisitFuncT]` isn't present in the function's parameters.
) -> Callable[[_CSTVisitFuncT], _CSTVisitFuncT]:
    """
    A decorator for visit and leave methods inside a :class:`MatcherDecoratableTransformer`
    or a :class:`MatcherDecoratableVisitor`. A method that is decorated with this decorator
    will only be called if it or one of its parents matches the supplied matcher.
    Use this to selectively gate visit and leave methods to be called only when
    inside of another relevant node. Note that this works for both node and attribute
    methods, so you can decorate a ``visit_<Node>`` or a ``visit_<Node>_<Attr>`` method.
    """

    def inner(original: _CSTVisitFuncT) -> _CSTVisitFuncT:
        setattr(
            original,
            VISIT_POSITIVE_MATCHER_ATTR,
            [*getattr(original, VISIT_POSITIVE_MATCHER_ATTR, []), matcher],
        )
        return original

    return inner


def call_if_not_inside(
    matcher: BaseMatcherNode,
    # pyre-fixme[34]: `Variable[_CSTVisitFuncT]` isn't present in the function's parameters.
) -> Callable[[_CSTVisitFuncT], _CSTVisitFuncT]:
    """
    A decorator for visit and leave methods inside a :class:`MatcherDecoratableTransformer`
    or a :class:`MatcherDecoratableVisitor`. A method that is decorated with this decorator
    will only be called if it or one of its parents does not match the supplied
    matcher. Use this to selectively gate visit and leave methods to be called only
    when outside of another relevant node. Note that this works for both node and
    attribute methods, so you can decorate a ``visit_<Node>`` or a ``visit_<Node>_<Attr>``
    method.
    """

    def inner(original: _CSTVisitFuncT) -> _CSTVisitFuncT:
        setattr(
            original,
            VISIT_NEGATIVE_MATCHER_ATTR,
            [*getattr(original, VISIT_NEGATIVE_MATCHER_ATTR, []), matcher],
        )
        return original

    return inner


# pyre-fixme[34]: `Variable[_CSTVisitFuncT]` isn't present in the function's parameters.
def visit(matcher: BaseMatcherNode) -> Callable[[_CSTVisitFuncT], _CSTVisitFuncT]:
    """
    A decorator that allows a method inside a :class:`MatcherDecoratableTransformer`
    or a :class:`MatcherDecoratableVisitor` visitor to be called when visiting a node
    that matches the provided matcher. Note that you can use this in combination with
    :func:`call_if_inside` and :func:`call_if_not_inside` decorators. Unlike explicit
    ``visit_<Node>`` and ``leave_<Node>`` methods, functions decorated with this
    decorator cannot stop child traversal by returning ``False``. Decorated visit
    functions should always have a return annotation of ``None``.

    There is no restriction on the number of visit decorators allowed on a method.
    There is also no restriction on the number of methods that may be decorated
    with the same matcher. When multiple visit decorators are found on the same
    method, they act as a simple or, and the method will be called when any one
    of the contained matches is ``True``.
    """

    def inner(original: _CSTVisitFuncT) -> _CSTVisitFuncT:
        setattr(
            original,
            CONSTRUCTED_VISIT_MATCHER_ATTR,
            [*getattr(original, CONSTRUCTED_VISIT_MATCHER_ATTR, []), matcher],
        )
        return original

    return inner


# pyre-fixme[34]: `Variable[_CSTVisitFuncT]` isn't present in the function's parameters.
def leave(matcher: BaseMatcherNode) -> Callable[[_CSTVisitFuncT], _CSTVisitFuncT]:
    """
    A decorator that allows a method inside a :class:`MatcherDecoratableTransformer`
    or a :class:`MatcherDecoratableVisitor` visitor to be called when leaving a node
    that matches the provided matcher. Note that you can use this in combination
    with :func:`call_if_inside` and :func:`call_if_not_inside` decorators.

    There is no restriction on the number of leave decorators allowed on a method.
    There is also no restriction on the number of methods that may be decorated
    with the same matcher. When multiple leave decorators are found on the same
    method, they act as a simple or, and the method will be called when any one
    of the contained matches is ``True``.
    """

    def inner(original: _CSTVisitFuncT) -> _CSTVisitFuncT:
        setattr(
            original,
            CONSTRUCTED_LEAVE_MATCHER_ATTR,
            [*getattr(original, CONSTRUCTED_LEAVE_MATCHER_ATTR, []), matcher],
        )
        return original

    return inner
