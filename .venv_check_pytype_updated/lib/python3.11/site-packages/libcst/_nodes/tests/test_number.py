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


class NumberTest(CSTNodeTest):
    @data_provider(
        (
            # Simple number
            (cst.Integer("5"), "5", parse_expression),
            # Negted number
            (
                cst.UnaryOperation(operator=cst.Minus(), expression=cst.Integer("5")),
                "-5",
                parse_expression,
                CodeRange((1, 0), (1, 2)),
            ),
            # In parenthesis
            (
                cst.UnaryOperation(
                    lpar=(cst.LeftParen(),),
                    operator=cst.Minus(),
                    expression=cst.Integer("5"),
                    rpar=(cst.RightParen(),),
                ),
                "(-5)",
                parse_expression,
                CodeRange((1, 1), (1, 3)),
            ),
            (
                cst.UnaryOperation(
                    lpar=(cst.LeftParen(),),
                    operator=cst.Minus(),
                    expression=cst.Integer(
                        "5", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                    ),
                    rpar=(cst.RightParen(),),
                ),
                "(-(5))",
                parse_expression,
                CodeRange((1, 1), (1, 5)),
            ),
            (
                cst.UnaryOperation(
                    operator=cst.Minus(),
                    expression=cst.UnaryOperation(
                        operator=cst.Minus(), expression=cst.Integer("5")
                    ),
                ),
                "--5",
                parse_expression,
                CodeRange((1, 0), (1, 3)),
            ),
            # multiple nested parenthesis
            (
                cst.Integer(
                    "5",
                    lpar=(cst.LeftParen(), cst.LeftParen()),
                    rpar=(cst.RightParen(), cst.RightParen()),
                ),
                "((5))",
                parse_expression,
                CodeRange((1, 2), (1, 3)),
            ),
            (
                cst.UnaryOperation(
                    lpar=(cst.LeftParen(),),
                    operator=cst.Plus(),
                    expression=cst.Integer(
                        "5",
                        lpar=(cst.LeftParen(), cst.LeftParen()),
                        rpar=(cst.RightParen(), cst.RightParen()),
                    ),
                    rpar=(cst.RightParen(),),
                ),
                "(+((5)))",
                parse_expression,
                CodeRange((1, 1), (1, 7)),
            ),
        )
    )
    def test_valid(
        self,
        node: cst.CSTNode,
        code: str,
        parser: Optional[Callable[[str], cst.CSTNode]],
        position: Optional[CodeRange] = None,
    ) -> None:
        self.validate_node(node, code, parser, expected_position=position)

    @data_provider(
        (
            (
                lambda: cst.Integer("5", lpar=(cst.LeftParen(),)),
                "left paren without right paren",
            ),
            (
                lambda: cst.Integer("5", rpar=(cst.RightParen(),)),
                "right paren without left paren",
            ),
            (
                lambda: cst.Float("5.5", lpar=(cst.LeftParen(),)),
                "left paren without right paren",
            ),
            (
                lambda: cst.Float("5.5", rpar=(cst.RightParen(),)),
                "right paren without left paren",
            ),
            (
                lambda: cst.Imaginary("5i", lpar=(cst.LeftParen(),)),
                "left paren without right paren",
            ),
            (
                lambda: cst.Imaginary("5i", rpar=(cst.RightParen(),)),
                "right paren without left paren",
            ),
        )
    )
    def test_invalid(
        self, get_node: Callable[[], cst.CSTNode], expected_re: str
    ) -> None:
        self.assert_invalid(get_node, expected_re)
