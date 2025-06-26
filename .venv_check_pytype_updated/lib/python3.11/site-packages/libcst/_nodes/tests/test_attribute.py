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


class AttributeTest(CSTNodeTest):
    @data_provider(
        (
            # Simple attribute access
            {
                "node": cst.Attribute(cst.Name("foo"), cst.Name("bar")),
                "code": "foo.bar",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 0), (1, 7)),
            },
            # Parenthesized attribute access
            {
                "node": cst.Attribute(
                    lpar=(cst.LeftParen(),),
                    value=cst.Name("foo"),
                    attr=cst.Name("bar"),
                    rpar=(cst.RightParen(),),
                ),
                "code": "(foo.bar)",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 1), (1, 8)),
            },
            # Make sure that spacing works
            {
                "node": cst.Attribute(
                    lpar=(cst.LeftParen(whitespace_after=cst.SimpleWhitespace(" ")),),
                    value=cst.Name("foo"),
                    dot=cst.Dot(
                        whitespace_before=cst.SimpleWhitespace(" "),
                        whitespace_after=cst.SimpleWhitespace(" "),
                    ),
                    attr=cst.Name("bar"),
                    rpar=(cst.RightParen(whitespace_before=cst.SimpleWhitespace(" ")),),
                ),
                "code": "( foo . bar )",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 2), (1, 11)),
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            {
                "get_node": (
                    lambda: cst.Attribute(
                        cst.Name("foo"), cst.Name("bar"), lpar=(cst.LeftParen(),)
                    )
                ),
                "expected_re": "left paren without right paren",
            },
            {
                "get_node": (
                    lambda: cst.Attribute(
                        cst.Name("foo"), cst.Name("bar"), rpar=(cst.RightParen(),)
                    )
                ),
                "expected_re": "right paren without left paren",
            },
        )
    )
    def test_invalid(self, **kwargs: Any) -> None:
        self.assert_invalid(**kwargs)
