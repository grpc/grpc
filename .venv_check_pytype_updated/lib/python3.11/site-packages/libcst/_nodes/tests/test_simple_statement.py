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


class SimpleStatementTest(CSTNodeTest):
    @data_provider(
        (
            # a single-element SimpleStatementLine
            {
                "node": cst.SimpleStatementLine((cst.Pass(),)),
                "code": "pass\n",
                "parser": parse_statement,
            },
            # a multi-element SimpleStatementLine
            {
                "node": cst.SimpleStatementLine(
                    (cst.Pass(semicolon=cst.Semicolon()), cst.Continue())
                ),
                "code": "pass;continue\n",
                "parser": parse_statement,
            },
            # a multi-element SimpleStatementLine with whitespace
            {
                "node": cst.SimpleStatementLine(
                    (
                        cst.Pass(
                            semicolon=cst.Semicolon(
                                whitespace_before=cst.SimpleWhitespace(" "),
                                whitespace_after=cst.SimpleWhitespace("  "),
                            )
                        ),
                        cst.Continue(),
                    )
                ),
                "code": "pass ;  continue\n",
                "parser": parse_statement,
            },
            # A more complicated SimpleStatementLine
            {
                "node": cst.SimpleStatementLine(
                    (
                        cst.Pass(semicolon=cst.Semicolon()),
                        cst.Continue(semicolon=cst.Semicolon()),
                        cst.Break(),
                    )
                ),
                "code": "pass;continue;break\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (1, 19)),
            },
            # a multi-element SimpleStatementLine, inferred semicolons
            {
                "node": cst.SimpleStatementLine(
                    (cst.Pass(), cst.Continue(), cst.Break())
                ),
                "code": "pass; continue; break\n",
                "parser": None,  # No test for parsing, since we are using sentinels.
            },
            # some expression statements
            {
                "node": cst.SimpleStatementLine((cst.Expr(cst.Name("None")),)),
                "code": "None\n",
                "parser": parse_statement,
            },
            {
                "node": cst.SimpleStatementLine((cst.Expr(cst.Name("True")),)),
                "code": "True\n",
                "parser": parse_statement,
            },
            {
                "node": cst.SimpleStatementLine((cst.Expr(cst.Name("False")),)),
                "code": "False\n",
                "parser": parse_statement,
            },
            {
                "node": cst.SimpleStatementLine((cst.Expr(cst.Ellipsis()),)),
                "code": "...\n",
                "parser": parse_statement,
            },
            # Test some numbers
            {
                "node": cst.SimpleStatementLine((cst.Expr(cst.Integer("5")),)),
                "code": "5\n",
                "parser": parse_statement,
            },
            {
                "node": cst.SimpleStatementLine((cst.Expr(cst.Float("5.5")),)),
                "code": "5.5\n",
                "parser": parse_statement,
            },
            {
                "node": cst.SimpleStatementLine((cst.Expr(cst.Imaginary("5j")),)),
                "code": "5j\n",
                "parser": parse_statement,
            },
            # Test some numbers with parens
            {
                "node": cst.SimpleStatementLine(
                    (
                        cst.Expr(
                            cst.Integer(
                                "5", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                            )
                        ),
                    )
                ),
                "code": "(5)\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (1, 3)),
            },
            {
                "node": cst.SimpleStatementLine(
                    (
                        cst.Expr(
                            cst.Float(
                                "5.5", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                            )
                        ),
                    )
                ),
                "code": "(5.5)\n",
                "parser": parse_statement,
            },
            {
                "node": cst.SimpleStatementLine(
                    (
                        cst.Expr(
                            cst.Imaginary(
                                "5j", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                            )
                        ),
                    )
                ),
                "code": "(5j)\n",
                "parser": parse_statement,
            },
            # Test some strings
            {
                "node": cst.SimpleStatementLine((cst.Expr(cst.SimpleString('"abc"')),)),
                "code": '"abc"\n',
                "parser": parse_statement,
            },
            {
                "node": cst.SimpleStatementLine(
                    (
                        cst.Expr(
                            cst.ConcatenatedString(
                                cst.SimpleString('"abc"'), cst.SimpleString('"def"')
                            )
                        ),
                    )
                ),
                "code": '"abc""def"\n',
                "parser": parse_statement,
            },
            {
                "node": cst.SimpleStatementLine(
                    (
                        cst.Expr(
                            cst.ConcatenatedString(
                                left=cst.SimpleString('"abc"'),
                                whitespace_between=cst.SimpleWhitespace(" "),
                                right=cst.ConcatenatedString(
                                    left=cst.SimpleString('"def"'),
                                    whitespace_between=cst.SimpleWhitespace(" "),
                                    right=cst.SimpleString('"ghi"'),
                                ),
                            )
                        ),
                    )
                ),
                "code": '"abc" "def" "ghi"\n',
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (1, 17)),
            },
            # Test parenthesis rules
            {
                "node": cst.SimpleStatementLine(
                    (
                        cst.Expr(
                            cst.Ellipsis(
                                lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                            )
                        ),
                    )
                ),
                "code": "(...)\n",
                "parser": parse_statement,
            },
            # Test parenthesis with whitespace ownership
            {
                "node": cst.SimpleStatementLine(
                    (
                        cst.Expr(
                            cst.Ellipsis(
                                lpar=(
                                    cst.LeftParen(
                                        whitespace_after=cst.SimpleWhitespace(" ")
                                    ),
                                ),
                                rpar=(
                                    cst.RightParen(
                                        whitespace_before=cst.SimpleWhitespace(" ")
                                    ),
                                ),
                            )
                        ),
                    )
                ),
                "code": "( ... )\n",
                "parser": parse_statement,
            },
            {
                "node": cst.SimpleStatementLine(
                    (
                        cst.Expr(
                            cst.Ellipsis(
                                lpar=(
                                    cst.LeftParen(
                                        whitespace_after=cst.SimpleWhitespace(" ")
                                    ),
                                    cst.LeftParen(
                                        whitespace_after=cst.SimpleWhitespace("  ")
                                    ),
                                    cst.LeftParen(
                                        whitespace_after=cst.SimpleWhitespace("   ")
                                    ),
                                ),
                                rpar=(
                                    cst.RightParen(
                                        whitespace_before=cst.SimpleWhitespace("   ")
                                    ),
                                    cst.RightParen(
                                        whitespace_before=cst.SimpleWhitespace("  ")
                                    ),
                                    cst.RightParen(
                                        whitespace_before=cst.SimpleWhitespace(" ")
                                    ),
                                ),
                            )
                        ),
                    )
                ),
                "code": "( (  (   ...   )  ) )\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (1, 21)),
            },
            # Test parenthesis rules with expressions
            {
                "node": cst.SimpleStatementLine(
                    (
                        cst.Expr(
                            cst.Ellipsis(
                                lpar=(
                                    cst.LeftParen(
                                        whitespace_after=cst.ParenthesizedWhitespace(
                                            first_line=cst.TrailingWhitespace(),
                                            empty_lines=(
                                                cst.EmptyLine(
                                                    comment=cst.Comment(
                                                        "# Wow, a comment!"
                                                    )
                                                ),
                                            ),
                                            indent=True,
                                            last_line=cst.SimpleWhitespace("    "),
                                        )
                                    ),
                                ),
                                rpar=(
                                    cst.RightParen(
                                        whitespace_before=cst.ParenthesizedWhitespace(
                                            first_line=cst.TrailingWhitespace(),
                                            empty_lines=(),
                                            indent=True,
                                            last_line=cst.SimpleWhitespace(""),
                                        )
                                    ),
                                ),
                            )
                        ),
                    )
                ),
                "code": "(\n# Wow, a comment!\n    ...\n)\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (4, 1)),
            },
            # test trailing whitespace
            {
                "node": cst.SimpleStatementLine(
                    (cst.Pass(),),
                    trailing_whitespace=cst.TrailingWhitespace(
                        whitespace=cst.SimpleWhitespace("  "),
                        comment=cst.Comment("# trailing comment"),
                    ),
                ),
                "code": "pass  # trailing comment\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (1, 4)),
            },
            # test leading comment
            {
                "node": cst.SimpleStatementLine(
                    (cst.Pass(),),
                    leading_lines=(cst.EmptyLine(comment=cst.Comment("# comment")),),
                ),
                "code": "# comment\npass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((2, 0), (2, 4)),
            },
            # test indentation
            {
                "node": DummyIndentedBlock(
                    "    ",
                    cst.SimpleStatementLine(
                        (cst.Pass(),),
                        leading_lines=(
                            cst.EmptyLine(comment=cst.Comment("# comment")),
                        ),
                    ),
                ),
                "code": "    # comment\n    pass\n",
                "expected_position": CodeRange((2, 4), (2, 8)),
            },
            # test suite variant
            {
                "node": cst.SimpleStatementSuite((cst.Pass(),)),
                "code": " pass\n",
                "expected_position": CodeRange((1, 1), (1, 5)),
            },
            {
                "node": cst.SimpleStatementSuite(
                    (cst.Pass(),), leading_whitespace=cst.SimpleWhitespace("")
                ),
                "code": "pass\n",
                "expected_position": CodeRange((1, 0), (1, 4)),
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)
