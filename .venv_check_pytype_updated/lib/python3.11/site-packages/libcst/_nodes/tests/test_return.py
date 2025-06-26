# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any

import libcst as cst
from libcst import parse_statement
from libcst._nodes.tests.base import CSTNodeTest
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider


class ReturnCreateTest(CSTNodeTest):
    @data_provider(
        (
            {
                "node": cst.SimpleStatementLine([cst.Return()]),
                "code": "return\n",
                "expected_position": CodeRange((1, 0), (1, 6)),
            },
            {
                "node": cst.SimpleStatementLine([cst.Return(cst.Name("abc"))]),
                "code": "return abc\n",
                "expected_position": CodeRange((1, 0), (1, 10)),
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            {
                "get_node": lambda: cst.Return(
                    cst.Name("abc"), whitespace_after_return=cst.SimpleWhitespace("")
                ),
                "expected_re": "Must have at least one space after 'return'.",
            },
        )
    )
    def test_invalid(self, **kwargs: Any) -> None:
        self.assert_invalid(**kwargs)


class ReturnParseTest(CSTNodeTest):
    @data_provider(
        (
            {
                "node": cst.SimpleStatementLine(
                    [cst.Return(whitespace_after_return=cst.SimpleWhitespace(""))]
                ),
                "code": "return\n",
                "parser": parse_statement,
            },
            {
                "node": cst.SimpleStatementLine(
                    [
                        cst.Return(
                            cst.Name("abc"),
                            whitespace_after_return=cst.SimpleWhitespace(" "),
                        )
                    ]
                ),
                "code": "return abc\n",
                "parser": parse_statement,
            },
            {
                "node": cst.SimpleStatementLine(
                    [
                        cst.Return(
                            cst.Name("abc"),
                            whitespace_after_return=cst.SimpleWhitespace("   "),
                        )
                    ]
                ),
                "code": "return   abc\n",
                "parser": parse_statement,
            },
            {
                "node": cst.SimpleStatementLine(
                    [
                        cst.Return(
                            cst.Name(
                                "abc", lpar=[cst.LeftParen()], rpar=[cst.RightParen()]
                            ),
                            whitespace_after_return=cst.SimpleWhitespace(""),
                        )
                    ]
                ),
                "code": "return(abc)\n",
                "parser": parse_statement,
            },
            {
                "node": cst.SimpleStatementLine(
                    [
                        cst.Return(
                            cst.Name("abc"),
                            whitespace_after_return=cst.SimpleWhitespace(" "),
                            semicolon=cst.Semicolon(),
                        )
                    ]
                ),
                "code": "return abc;\n",
                "parser": parse_statement,
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)
