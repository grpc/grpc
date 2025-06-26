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


class DelTest(CSTNodeTest):
    @data_provider(
        (
            {
                "node": cst.SimpleStatementLine([cst.Del(cst.Name("abc"))]),
                "code": "del abc\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (1, 7)),
            },
            {
                "node": cst.SimpleStatementLine(
                    [
                        cst.Del(
                            cst.Name("abc"),
                            whitespace_after_del=cst.SimpleWhitespace("   "),
                        )
                    ]
                ),
                "code": "del   abc\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (1, 9)),
            },
            {
                "node": cst.SimpleStatementLine(
                    [
                        cst.Del(
                            cst.Name(
                                "abc", lpar=[cst.LeftParen()], rpar=[cst.RightParen()]
                            ),
                            whitespace_after_del=cst.SimpleWhitespace(""),
                        )
                    ]
                ),
                "code": "del(abc)\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (1, 8)),
            },
            {
                "node": cst.SimpleStatementLine(
                    [cst.Del(cst.Name("abc"), semicolon=cst.Semicolon())]
                ),
                "code": "del abc;\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (1, 7)),
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            {
                "get_node": lambda: cst.Del(
                    cst.Name("abc"), whitespace_after_del=cst.SimpleWhitespace("")
                ),
                "expected_re": "Must have at least one space after 'del'.",
            },
        )
    )
    def test_invalid(self, **kwargs: Any) -> None:
        self.assert_invalid(**kwargs)
