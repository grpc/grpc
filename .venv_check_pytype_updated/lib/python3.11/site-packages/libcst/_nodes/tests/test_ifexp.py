# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Callable, Optional

import libcst as cst
from libcst import parse_expression
from libcst._nodes.tests.base import CSTNodeTest
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider


class IfExpTest(CSTNodeTest):
    @data_provider(
        (
            # Simple if experessions
            (
                cst.IfExp(
                    body=cst.Name("foo"), test=cst.Name("bar"), orelse=cst.Name("baz")
                ),
                "foo if bar else baz",
            ),
            # Parenthesized if expressions
            (
                cst.IfExp(
                    lpar=(cst.LeftParen(),),
                    body=cst.Name("foo"),
                    test=cst.Name("bar"),
                    orelse=cst.Name("baz"),
                    rpar=(cst.RightParen(),),
                ),
                "(foo if bar else baz)",
            ),
            (
                cst.IfExp(
                    body=cst.Name(
                        "foo", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                    ),
                    whitespace_before_if=cst.SimpleWhitespace(""),
                    whitespace_after_if=cst.SimpleWhitespace(""),
                    test=cst.Name(
                        "bar", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                    ),
                    whitespace_before_else=cst.SimpleWhitespace(""),
                    whitespace_after_else=cst.SimpleWhitespace(""),
                    orelse=cst.Name(
                        "baz", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                    ),
                ),
                "(foo)if(bar)else(baz)",
                CodeRange((1, 0), (1, 21)),
            ),
            (
                cst.IfExp(
                    body=cst.Name("foo"),
                    whitespace_before_if=cst.SimpleWhitespace(" "),
                    whitespace_after_if=cst.SimpleWhitespace(" "),
                    test=cst.Name("bar"),
                    whitespace_before_else=cst.SimpleWhitespace(" "),
                    whitespace_after_else=cst.SimpleWhitespace(""),
                    orelse=cst.IfExp(
                        body=cst.SimpleString("''"),
                        whitespace_before_if=cst.SimpleWhitespace(""),
                        test=cst.Name("bar"),
                        orelse=cst.Name("baz"),
                    ),
                ),
                "foo if bar else''if bar else baz",
                CodeRange((1, 0), (1, 32)),
            ),
            (
                cst.GeneratorExp(
                    elt=cst.IfExp(
                        body=cst.Name("foo"),
                        test=cst.Name("bar"),
                        orelse=cst.SimpleString("''"),
                        whitespace_after_else=cst.SimpleWhitespace(""),
                    ),
                    for_in=cst.CompFor(
                        target=cst.Name("_"),
                        iter=cst.Name("_"),
                        whitespace_before=cst.SimpleWhitespace(""),
                    ),
                ),
                "(foo if bar else''for _ in _)",
                CodeRange((1, 1), (1, 28)),
            ),
            # Make sure that spacing works
            (
                cst.IfExp(
                    lpar=(cst.LeftParen(whitespace_after=cst.SimpleWhitespace(" ")),),
                    body=cst.Name("foo"),
                    whitespace_before_if=cst.SimpleWhitespace("  "),
                    whitespace_after_if=cst.SimpleWhitespace("  "),
                    test=cst.Name("bar"),
                    whitespace_before_else=cst.SimpleWhitespace("  "),
                    whitespace_after_else=cst.SimpleWhitespace("  "),
                    orelse=cst.Name("baz"),
                    rpar=(cst.RightParen(whitespace_before=cst.SimpleWhitespace(" ")),),
                ),
                "( foo  if  bar  else  baz )",
                CodeRange((1, 2), (1, 25)),
            ),
        )
    )
    def test_valid(
        self, node: cst.CSTNode, code: str, position: Optional[CodeRange] = None
    ) -> None:
        self.validate_node(node, code, parse_expression, expected_position=position)

    @data_provider(
        (
            (
                lambda: cst.IfExp(
                    cst.Name("bar"),
                    cst.Name("foo"),
                    cst.Name("baz"),
                    lpar=(cst.LeftParen(),),
                ),
                "left paren without right paren",
            ),
            (
                lambda: cst.IfExp(
                    cst.Name("bar"),
                    cst.Name("foo"),
                    cst.Name("baz"),
                    rpar=(cst.RightParen(),),
                ),
                "right paren without left paren",
            ),
        )
    )
    def test_invalid(
        self, get_node: Callable[[], cst.CSTNode], expected_re: str
    ) -> None:
        self.assert_invalid(get_node, expected_re)
