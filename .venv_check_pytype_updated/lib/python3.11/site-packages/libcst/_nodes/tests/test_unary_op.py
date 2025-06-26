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


class UnaryOperationTest(CSTNodeTest):
    @data_provider(
        (
            # Simple unary operations
            (cst.UnaryOperation(cst.Plus(), cst.Name("foo")), "+foo"),
            (cst.UnaryOperation(cst.Minus(), cst.Name("foo")), "-foo"),
            (cst.UnaryOperation(cst.BitInvert(), cst.Name("foo")), "~foo"),
            (cst.UnaryOperation(cst.Not(), cst.Name("foo")), "not foo"),
            # Parenthesized unary operation
            (
                cst.UnaryOperation(
                    lpar=(cst.LeftParen(),),
                    operator=cst.Not(),
                    expression=cst.Name("foo"),
                    rpar=(cst.RightParen(),),
                ),
                "(not foo)",
                CodeRange((1, 1), (1, 8)),
            ),
            (
                cst.UnaryOperation(
                    operator=cst.Not(whitespace_after=cst.SimpleWhitespace("")),
                    expression=cst.Name(
                        "foo", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                    ),
                ),
                "not(foo)",
                CodeRange((1, 0), (1, 8)),
            ),
            # Make sure that spacing works
            (
                cst.UnaryOperation(
                    lpar=(cst.LeftParen(whitespace_after=cst.SimpleWhitespace(" ")),),
                    operator=cst.Not(whitespace_after=cst.SimpleWhitespace("  ")),
                    expression=cst.Name("foo"),
                    rpar=(cst.RightParen(whitespace_before=cst.SimpleWhitespace(" ")),),
                ),
                "( not  foo )",
                CodeRange((1, 2), (1, 10)),
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
                lambda: cst.UnaryOperation(
                    cst.Plus(), cst.Name("foo"), lpar=(cst.LeftParen(),)
                ),
                "left paren without right paren",
            ),
            (
                lambda: cst.UnaryOperation(
                    cst.Plus(), cst.Name("foo"), rpar=(cst.RightParen(),)
                ),
                "right paren without left paren",
            ),
            (
                lambda: cst.UnaryOperation(
                    operator=cst.Not(whitespace_after=cst.SimpleWhitespace("")),
                    expression=cst.Name("foo"),
                ),
                "at least one space after not operator",
            ),
        )
    )
    def test_invalid(
        self, get_node: Callable[[], cst.CSTNode], expected_re: str
    ) -> None:
        self.assert_invalid(get_node, expected_re)
