# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Callable

import libcst as cst
from libcst._nodes.tests.base import CSTNodeTest, DummyIndentedBlock
from libcst.testing.utils import data_provider


class SimpleWhitespaceTest(CSTNodeTest):
    @data_provider(
        (
            (cst.SimpleWhitespace(""), ""),
            (cst.SimpleWhitespace(" "), " "),
            (cst.SimpleWhitespace(" \t\f"), " \t\f"),
            (cst.SimpleWhitespace("\\\n "), "\\\n "),
            (cst.SimpleWhitespace("\\\r\n "), "\\\r\n "),
        )
    )
    def test_valid(self, node: cst.CSTNode, code: str) -> None:
        self.validate_node(node, code)

    @data_provider(
        (
            (lambda: cst.SimpleWhitespace(" bad input"), "non-whitespace"),
            (lambda: cst.SimpleWhitespace("\\"), "non-whitespace"),
            (lambda: cst.SimpleWhitespace("\\\n\n "), "non-whitespace"),
        )
    )
    def test_invalid(
        self, get_node: Callable[[], cst.CSTNode], expected_re: str
    ) -> None:
        self.assert_invalid(get_node, expected_re)


class ParenthesizedWhitespaceTest(CSTNodeTest):
    @data_provider(
        (
            (cst.ParenthesizedWhitespace(), "\n"),
            (
                cst.ParenthesizedWhitespace(
                    first_line=cst.TrailingWhitespace(
                        cst.SimpleWhitespace("   "), cst.Comment("# This is a comment")
                    )
                ),
                "   # This is a comment\n",
            ),
            (
                cst.ParenthesizedWhitespace(
                    first_line=cst.TrailingWhitespace(
                        cst.SimpleWhitespace("   "), cst.Comment("# This is a comment")
                    ),
                    empty_lines=(cst.EmptyLine(), cst.EmptyLine(), cst.EmptyLine()),
                ),
                "   # This is a comment\n\n\n\n",
            ),
            (
                cst.ParenthesizedWhitespace(
                    first_line=cst.TrailingWhitespace(
                        cst.SimpleWhitespace("   "), cst.Comment("# This is a comment")
                    ),
                    empty_lines=(cst.EmptyLine(), cst.EmptyLine(), cst.EmptyLine()),
                    indent=False,
                    last_line=cst.SimpleWhitespace(" "),
                ),
                "   # This is a comment\n\n\n\n ",
            ),
            (
                DummyIndentedBlock(
                    "    ",
                    cst.ParenthesizedWhitespace(
                        first_line=cst.TrailingWhitespace(
                            cst.SimpleWhitespace("   "),
                            cst.Comment("# This is a comment"),
                        ),
                        empty_lines=(cst.EmptyLine(), cst.EmptyLine(), cst.EmptyLine()),
                        indent=True,
                        last_line=cst.SimpleWhitespace(" "),
                    ),
                ),
                "   # This is a comment\n    \n    \n    \n     ",
            ),
            (
                DummyIndentedBlock(
                    "    ",
                    cst.ParenthesizedWhitespace(
                        first_line=cst.TrailingWhitespace(
                            cst.SimpleWhitespace("   "),
                            cst.Comment("# This is a comment"),
                        ),
                        indent=True,
                        last_line=cst.SimpleWhitespace(""),
                    ),
                ),
                "   # This is a comment\n    ",
            ),
        )
    )
    def test_valid(self, node: cst.CSTNode, code: str) -> None:
        self.validate_node(node, code)
