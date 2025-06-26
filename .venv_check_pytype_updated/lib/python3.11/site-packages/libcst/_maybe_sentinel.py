# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from enum import auto, Enum


class MaybeSentinel(Enum):
    """
    A :class:`MaybeSentinel` value is used as the default value for some attributes to
    denote that when generating code (when :attr:`Module.code` is evaluated) we should
    optionally include this element in order to generate valid code.

    :class:`MaybeSentinel` is only used for "syntactic trivia" that most users shouldn't
    care much about anyways, like commas, semicolons, and whitespace.

    For example, a function call's :attr:`Arg.comma` value defaults to
    :attr:`MaybeSentinel.DEFAULT`. A comma is required after every argument, except for
    the last one. If a comma is required and :attr:`Arg.comma` is a
    :class:`MaybeSentinel`, one is inserted.

    This makes manual node construction easier, but it also means that we safely add
    arguments to a preexisting function call without manually fixing the commas:

    >>> import libcst as cst
    >>> fn_call = cst.parse_expression("fn(1, 2)")
    >>> new_fn_call = fn_call.with_changes(
    ...     args=[*fn_call.args, cst.Arg(cst.Integer("3"))]
    ... )
    >>> dummy_module = cst.parse_module("")  # we need to use Module.code_for_node
    >>> dummy_module.code_for_node(fn_call)
    'fn(1, 2)'
    >>> dummy_module.code_for_node(new_fn_call)
    'fn(1, 2, 3)'

    Notice that a comma was automatically inserted after the second argument. Since the
    original second argument had no comma, it was initialized to
    :attr:`MaybeSentinel.DEFAULT`. During the code generation of the second argument, a
    comma was inserted to ensure that the resulting code is valid.

    .. warning::
       While this sentinel is used in place of nodes, it is not a :class:`CSTNode`, and
       will not be visited by a :class:`CSTVisitor`.

    Some other libraries, like `RedBaron`_, take other approaches to this problem.
    RedBaron's tree is mutable (LibCST's tree is immutable), and so they're able to
    solve this problem with `"proxy lists"
    <http://redbaron.pycqa.org/en/latest/proxy_list.html>`_. Both approaches come with
    different sets of tradeoffs.

    .. _RedBaron: http://redbaron.pycqa.org/en/latest/index.html
    """

    DEFAULT = auto()

    def __repr__(self) -> str:
        return str(self)
