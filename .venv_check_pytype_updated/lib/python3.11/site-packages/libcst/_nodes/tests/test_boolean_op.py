# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any

import libcst as cst
from libcst import parse_expression
from libcst._nodes.tests.base import CSTNodeTest
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider


class BooleanOperationTest(CSTNodeTest):
    @data_provider(
        (
            # Simple boolean operations
            {
                "node": cst.BooleanOperation(
                    cst.Name("foo"), cst.And(), cst.Name("bar")
                ),
                "code": "foo and bar",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.BooleanOperation(
                    cst.Name("foo"), cst.Or(), cst.Name("bar")
                ),
                "code": "foo or bar",
                "parser": parse_expression,
                "expected_position": None,
            },
            # Parenthesized boolean operation
            {
                "node": cst.BooleanOperation(
                    lpar=(cst.LeftParen(),),
                    left=cst.Name("foo"),
                    operator=cst.Or(),
                    right=cst.Name("bar"),
                    rpar=(cst.RightParen(),),
                ),
                "code": "(foo or bar)",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.BooleanOperation(
                    left=cst.Name(
                        "foo", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                    ),
                    operator=cst.Or(
                        whitespace_before=cst.SimpleWhitespace(""),
                        whitespace_after=cst.SimpleWhitespace(""),
                    ),
                    right=cst.Name(
                        "bar", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                    ),
                ),
                "code": "(foo)or(bar)",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 0), (1, 12)),
            },
            # Make sure that spacing works
            {
                "node": cst.BooleanOperation(
                    lpar=(cst.LeftParen(whitespace_after=cst.SimpleWhitespace(" ")),),
                    left=cst.Name("foo"),
                    operator=cst.And(
                        whitespace_before=cst.SimpleWhitespace("  "),
                        whitespace_after=cst.SimpleWhitespace("  "),
                    ),
                    right=cst.Name("bar"),
                    rpar=(cst.RightParen(whitespace_before=cst.SimpleWhitespace(" ")),),
                ),
                "code": "( foo  and  bar )",
                "parser": parse_expression,
                "expected_position": None,
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            {
                "get_node": lambda: cst.BooleanOperation(
                    cst.Name("foo"), cst.And(), cst.Name("bar"), lpar=(cst.LeftParen(),)
                ),
                "expected_re": "left paren without right paren",
            },
            {
                "get_node": lambda: cst.BooleanOperation(
                    cst.Name("foo"),
                    cst.And(),
                    cst.Name("bar"),
                    rpar=(cst.RightParen(),),
                ),
                "expected_re": "right paren without left paren",
            },
            {
                "get_node": lambda: cst.BooleanOperation(
                    left=cst.Name("foo"),
                    operator=cst.Or(
                        whitespace_before=cst.SimpleWhitespace(""),
                        whitespace_after=cst.SimpleWhitespace(""),
                    ),
                    right=cst.Name("bar"),
                ),
                "expected_re": "at least one space around boolean operator",
            },
        )
    )
    def test_invalid(self, **kwargs: Any) -> None:
        self.assert_invalid(**kwargs)
