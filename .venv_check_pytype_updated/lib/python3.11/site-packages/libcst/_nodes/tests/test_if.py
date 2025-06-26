# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any, Callable

import libcst as cst
from libcst import parse_statement
from libcst._nodes.tests.base import CSTNodeTest, DummyIndentedBlock
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider


class IfTest(CSTNodeTest):
    @data_provider(
        (
            # Simple if without elif or else
            {
                "node": cst.If(
                    cst.Name("conditional"), cst.SimpleStatementSuite((cst.Pass(),))
                ),
                "code": "if conditional: pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (1, 20)),
            },
            # else clause
            {
                "node": cst.If(
                    cst.Name("conditional"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    orelse=cst.Else(cst.SimpleStatementSuite((cst.Pass(),))),
                ),
                "code": "if conditional: pass\nelse: pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (2, 10)),
            },
            # elif clause
            {
                "node": cst.If(
                    cst.Name("conditional"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    orelse=cst.If(
                        cst.Name("other_conditional"),
                        cst.SimpleStatementSuite((cst.Pass(),)),
                        orelse=cst.Else(cst.SimpleStatementSuite((cst.Pass(),))),
                    ),
                ),
                "code": "if conditional: pass\nelif other_conditional: pass\nelse: pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (3, 10)),
            },
            # indentation
            {
                "node": DummyIndentedBlock(
                    "    ",
                    cst.If(
                        cst.Name("conditional"),
                        cst.SimpleStatementSuite((cst.Pass(),)),
                        orelse=cst.Else(cst.SimpleStatementSuite((cst.Pass(),))),
                    ),
                ),
                "code": "    if conditional: pass\n    else: pass\n",
                "parser": None,
                "expected_position": CodeRange((1, 4), (2, 14)),
            },
            # with an indented body
            {
                "node": DummyIndentedBlock(
                    "    ",
                    cst.If(
                        cst.Name("conditional"),
                        cst.IndentedBlock((cst.SimpleStatementLine((cst.Pass(),)),)),
                    ),
                ),
                "code": "    if conditional:\n        pass\n",
                "parser": None,
                "expected_position": CodeRange((1, 4), (2, 12)),
            },
            # leading_lines
            {
                "node": cst.If(
                    cst.Name("conditional"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    leading_lines=(
                        cst.EmptyLine(comment=cst.Comment("# leading comment")),
                    ),
                ),
                "code": "# leading comment\nif conditional: pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((2, 0), (2, 20)),
            },
            # whitespace before/after test and else
            {
                "node": cst.If(
                    cst.Name("conditional"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    whitespace_before_test=cst.SimpleWhitespace("   "),
                    whitespace_after_test=cst.SimpleWhitespace("  "),
                    orelse=cst.Else(
                        cst.SimpleStatementSuite((cst.Pass(),)),
                        whitespace_before_colon=cst.SimpleWhitespace(" "),
                    ),
                ),
                "code": "if   conditional  : pass\nelse : pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (2, 11)),
            },
            # empty lines between if/elif/else clauses, not captured by the suite.
            {
                "node": cst.If(
                    cst.Name("test_a"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    orelse=cst.If(
                        cst.Name("test_b"),
                        cst.SimpleStatementSuite((cst.Pass(),)),
                        leading_lines=(cst.EmptyLine(),),
                        orelse=cst.Else(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            leading_lines=(cst.EmptyLine(),),
                        ),
                    ),
                ),
                "code": "if test_a: pass\n\nelif test_b: pass\n\nelse: pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (5, 10)),
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            # Validate whitespace handling
            (
                lambda: cst.If(
                    cst.Name("conditional"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    whitespace_before_test=cst.SimpleWhitespace(""),
                ),
                "Must have at least one space after 'if' keyword.",
            ),
        )
    )
    def test_invalid(
        self, get_node: Callable[[], cst.CSTNode], expected_re: str
    ) -> None:
        self.assert_invalid(get_node, expected_re)
