# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any

import libcst as cst
from libcst import parse_statement
from libcst._nodes.tests.base import CSTNodeTest, DummyIndentedBlock
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider


class WhileTest(CSTNodeTest):
    @data_provider(
        (
            # Simple while block
            {
                "node": cst.While(
                    cst.Call(cst.Name("iter")), cst.SimpleStatementSuite((cst.Pass(),))
                ),
                "code": "while iter(): pass\n",
                "parser": parse_statement,
            },
            # While block with else
            {
                "node": cst.While(
                    cst.Call(cst.Name("iter")),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    cst.Else(cst.SimpleStatementSuite((cst.Pass(),))),
                ),
                "code": "while iter(): pass\nelse: pass\n",
                "parser": parse_statement,
            },
            # indentation
            {
                "node": DummyIndentedBlock(
                    "    ",
                    cst.While(
                        cst.Call(cst.Name("iter")),
                        cst.SimpleStatementSuite((cst.Pass(),)),
                    ),
                ),
                "code": "    while iter(): pass\n",
                "parser": None,
                "expected_position": CodeRange((1, 4), (1, 22)),
            },
            # while an indented body
            {
                "node": DummyIndentedBlock(
                    "    ",
                    cst.While(
                        cst.Call(cst.Name("iter")),
                        cst.IndentedBlock((cst.SimpleStatementLine((cst.Pass(),)),)),
                    ),
                ),
                "code": "    while iter():\n        pass\n",
                "parser": None,
                "expected_position": CodeRange((1, 4), (2, 12)),
            },
            # leading_lines
            {
                "node": cst.While(
                    cst.Call(cst.Name("iter")),
                    cst.IndentedBlock((cst.SimpleStatementLine((cst.Pass(),)),)),
                    leading_lines=(
                        cst.EmptyLine(comment=cst.Comment("# leading comment")),
                    ),
                ),
                "code": "# leading comment\nwhile iter():\n    pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((2, 0), (3, 8)),
            },
            {
                "node": cst.While(
                    cst.Call(cst.Name("iter")),
                    cst.IndentedBlock((cst.SimpleStatementLine((cst.Pass(),)),)),
                    cst.Else(
                        cst.IndentedBlock((cst.SimpleStatementLine((cst.Pass(),)),)),
                        leading_lines=(
                            cst.EmptyLine(comment=cst.Comment("# else comment")),
                        ),
                    ),
                    leading_lines=(
                        cst.EmptyLine(comment=cst.Comment("# leading comment")),
                    ),
                ),
                "code": "# leading comment\nwhile iter():\n    pass\n# else comment\nelse:\n    pass\n",
                "parser": None,
                "expected_position": CodeRange((2, 0), (6, 8)),
            },
            # Weird spacing rules
            {
                "node": cst.While(
                    cst.Call(
                        cst.Name("iter"),
                        lpar=(cst.LeftParen(),),
                        rpar=(cst.RightParen(),),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    whitespace_after_while=cst.SimpleWhitespace(""),
                ),
                "code": "while(iter()): pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (1, 19)),
            },
            # Whitespace
            {
                "node": cst.While(
                    cst.Call(cst.Name("iter")),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    whitespace_after_while=cst.SimpleWhitespace("  "),
                    whitespace_before_colon=cst.SimpleWhitespace("  "),
                ),
                "code": "while  iter()  : pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (1, 21)),
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            {
                "get_node": lambda: cst.While(
                    cst.Call(cst.Name("iter")),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    whitespace_after_while=cst.SimpleWhitespace(""),
                ),
                "expected_re": "Must have at least one space after 'while' keyword",
            },
        )
    )
    def test_invalid(self, **kwargs: Any) -> None:
        self.assert_invalid(**kwargs)
