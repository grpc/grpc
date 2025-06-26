# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any

import libcst as cst
from libcst import parse_expression, parse_statement, PartialParserConfig
from libcst._nodes.tests.base import CSTNodeTest
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider


class AwaitTest(CSTNodeTest):
    @data_provider(
        (
            # Some simple calls
            {
                "node": cst.Await(cst.Name("test")),
                "code": "await test",
                "parser": lambda code: parse_expression(
                    code, config=PartialParserConfig(python_version="3.7")
                ),
                "expected_position": None,
            },
            {
                "node": cst.Await(cst.Call(cst.Name("test"))),
                "code": "await test()",
                "parser": lambda code: parse_expression(
                    code, config=PartialParserConfig(python_version="3.7")
                ),
                "expected_position": None,
            },
            # Whitespace
            {
                "node": cst.Await(
                    cst.Name("test"),
                    whitespace_after_await=cst.SimpleWhitespace("  "),
                    lpar=(cst.LeftParen(whitespace_after=cst.SimpleWhitespace(" ")),),
                    rpar=(cst.RightParen(whitespace_before=cst.SimpleWhitespace(" ")),),
                ),
                "code": "( await  test )",
                "parser": lambda code: parse_expression(
                    code, config=PartialParserConfig(python_version="3.7")
                ),
                "expected_position": CodeRange((1, 2), (1, 13)),
            },
            # Whitespace after await
            {
                "node": cst.Await(
                    cst.Name("foo", lpar=[cst.LeftParen()], rpar=[cst.RightParen()]),
                    whitespace_after_await=cst.SimpleWhitespace(""),
                ),
                "code": "await(foo)",
            },
        )
    )
    def test_valid_py37(self, **kwargs: Any) -> None:
        # We don't have sentinel nodes for atoms, so we know that 100% of atoms
        # can be parsed identically to their creation.
        self.validate_node(**kwargs)

    @data_provider(
        (
            # Some simple calls
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.IndentedBlock(
                        (
                            cst.SimpleStatementLine(
                                (cst.Expr(cst.Await(cst.Name("test"))),)
                            ),
                        )
                    ),
                    asynchronous=cst.Asynchronous(),
                ),
                "code": "async def foo():\n    await test\n",
                "parser": lambda code: parse_statement(
                    code, config=PartialParserConfig(python_version="3.6")
                ),
                "expected_position": None,
            },
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.IndentedBlock(
                        (
                            cst.SimpleStatementLine(
                                (cst.Expr(cst.Await(cst.Call(cst.Name("test")))),)
                            ),
                        )
                    ),
                    asynchronous=cst.Asynchronous(),
                ),
                "code": "async def foo():\n    await test()\n",
                "parser": lambda code: parse_statement(
                    code, config=PartialParserConfig(python_version="3.6")
                ),
                "expected_position": None,
            },
            # Whitespace
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.IndentedBlock(
                        (
                            cst.SimpleStatementLine(
                                (
                                    cst.Expr(
                                        cst.Await(
                                            cst.Name("test"),
                                            whitespace_after_await=cst.SimpleWhitespace(
                                                "  "
                                            ),
                                            lpar=(
                                                cst.LeftParen(
                                                    whitespace_after=cst.SimpleWhitespace(
                                                        " "
                                                    )
                                                ),
                                            ),
                                            rpar=(
                                                cst.RightParen(
                                                    whitespace_before=cst.SimpleWhitespace(
                                                        " "
                                                    )
                                                ),
                                            ),
                                        )
                                    ),
                                )
                            ),
                        )
                    ),
                    asynchronous=cst.Asynchronous(),
                ),
                "code": "async def foo():\n    ( await  test )\n",
                "parser": lambda code: parse_statement(
                    code, config=PartialParserConfig(python_version="3.6")
                ),
                "expected_position": None,
            },
        )
    )
    def test_valid_py36(self, **kwargs: Any) -> None:
        # We don't have sentinel nodes for atoms, so we know that 100% of atoms
        # can be parsed identically to their creation.
        self.validate_node(**kwargs)

    @data_provider(
        (
            # Expression wrapping parenthesis rules
            {
                "get_node": (
                    lambda: cst.Await(cst.Name("foo"), lpar=(cst.LeftParen(),))
                ),
                "expected_re": "left paren without right paren",
            },
            {
                "get_node": (
                    lambda: cst.Await(cst.Name("foo"), rpar=(cst.RightParen(),))
                ),
                "expected_re": "right paren without left paren",
            },
            {
                "get_node": (
                    lambda: cst.Await(
                        cst.Name("foo"), whitespace_after_await=cst.SimpleWhitespace("")
                    )
                ),
                "expected_re": "at least one space after await",
            },
        )
    )
    def test_invalid(self, **kwargs: Any) -> None:
        self.assert_invalid(**kwargs)
