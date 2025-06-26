# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any, Callable, Optional

import libcst as cst
from libcst import parse_statement
from libcst._nodes.tests.base import CSTNodeTest, parse_statement_as
from libcst._parser.entrypoints import is_native
from libcst.helpers import ensure_type
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider


class YieldConstructionTest(CSTNodeTest):
    @data_provider(
        (
            # Simple yield
            (cst.Yield(), "yield"),
            # yield expression
            (cst.Yield(cst.Name("a")), "yield a"),
            # yield from expression
            (cst.Yield(cst.From(cst.Call(cst.Name("a")))), "yield from a()"),
            # Parenthesizing tests
            (
                cst.Yield(
                    lpar=(cst.LeftParen(),),
                    value=cst.Integer("5"),
                    rpar=(cst.RightParen(),),
                ),
                "(yield 5)",
            ),
            # Whitespace oddities tests
            (
                cst.Yield(
                    cst.Name("a", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)),
                    whitespace_after_yield=cst.SimpleWhitespace(""),
                ),
                "yield(a)",
                CodeRange((1, 0), (1, 8)),
            ),
            (
                cst.Yield(
                    cst.From(
                        cst.Call(
                            cst.Name("a"),
                            lpar=(cst.LeftParen(),),
                            rpar=(cst.RightParen(),),
                        ),
                        whitespace_after_from=cst.SimpleWhitespace(""),
                    )
                ),
                "yield from(a())",
            ),
            # Whitespace rendering/parsing tests
            (
                cst.Yield(
                    lpar=(cst.LeftParen(whitespace_after=cst.SimpleWhitespace(" ")),),
                    value=cst.Integer("5"),
                    whitespace_after_yield=cst.SimpleWhitespace("  "),
                    rpar=(cst.RightParen(whitespace_before=cst.SimpleWhitespace(" ")),),
                ),
                "( yield  5 )",
            ),
            (
                cst.Yield(
                    lpar=(cst.LeftParen(whitespace_after=cst.SimpleWhitespace(" ")),),
                    value=cst.From(
                        cst.Call(cst.Name("bla")),
                        whitespace_after_from=cst.SimpleWhitespace("  "),
                    ),
                    whitespace_after_yield=cst.SimpleWhitespace("  "),
                    rpar=(cst.RightParen(whitespace_before=cst.SimpleWhitespace(" ")),),
                ),
                "( yield  from  bla() )",
                CodeRange((1, 2), (1, 20)),
            ),
            # From expression position tests
            (
                cst.From(
                    cst.Integer("5"), whitespace_after_from=cst.SimpleWhitespace(" ")
                ),
                "from 5",
                CodeRange((1, 0), (1, 6)),
            ),
        )
    )
    def test_valid(
        self, node: cst.CSTNode, code: str, position: Optional[CodeRange] = None
    ) -> None:
        self.validate_node(node, code, expected_position=position)

    @data_provider(
        (
            # Paren validation
            (
                lambda: cst.Yield(lpar=(cst.LeftParen(),)),
                "left paren without right paren",
            ),
            (
                lambda: cst.Yield(rpar=(cst.RightParen(),)),
                "right paren without left paren",
            ),
            # Make sure we have adequate space after yield
            (
                lambda: cst.Yield(
                    cst.Name("a"), whitespace_after_yield=cst.SimpleWhitespace("")
                ),
                "Must have at least one space after 'yield' keyword",
            ),
            (
                lambda: cst.Yield(
                    cst.From(cst.Call(cst.Name("a"))),
                    whitespace_after_yield=cst.SimpleWhitespace(""),
                ),
                "Must have at least one space after 'yield' keyword",
            ),
            # MAke sure we have adequate space after from
            (
                lambda: cst.Yield(
                    cst.From(
                        cst.Call(cst.Name("a")),
                        whitespace_after_from=cst.SimpleWhitespace(""),
                    )
                ),
                "Must have at least one space after 'from' keyword",
            ),
        )
    )
    def test_invalid(
        self, get_node: Callable[[], cst.CSTNode], expected_re: str
    ) -> None:
        self.assert_invalid(get_node, expected_re)


class YieldParsingTest(CSTNodeTest):
    @data_provider(
        (
            # Simple yield
            (cst.Yield(), "yield"),
            # yield expression
            (
                cst.Yield(
                    cst.Name("a"), whitespace_after_yield=cst.SimpleWhitespace(" ")
                ),
                "yield a",
            ),
            # yield from expression
            (
                cst.Yield(
                    cst.From(
                        cst.Call(cst.Name("a")),
                        whitespace_after_from=cst.SimpleWhitespace(" "),
                    ),
                    whitespace_after_yield=cst.SimpleWhitespace(" "),
                ),
                "yield from a()",
            ),
            # Parenthesizing tests
            (
                cst.Yield(
                    lpar=(cst.LeftParen(),),
                    whitespace_after_yield=cst.SimpleWhitespace(" "),
                    value=cst.Integer("5"),
                    rpar=(cst.RightParen(),),
                ),
                "(yield 5)",
            ),
            # Whitespace oddities tests
            (
                cst.Yield(
                    cst.Name("a", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)),
                    whitespace_after_yield=cst.SimpleWhitespace(""),
                ),
                "yield(a)",
            ),
            (
                cst.Yield(
                    cst.From(
                        cst.Call(
                            cst.Name("a"),
                            lpar=(cst.LeftParen(),),
                            rpar=(cst.RightParen(),),
                        ),
                        whitespace_after_from=cst.SimpleWhitespace(""),
                    ),
                    whitespace_after_yield=cst.SimpleWhitespace(" "),
                ),
                "yield from(a())",
            ),
            # Whitespace rendering/parsing tests
            (
                cst.Yield(
                    lpar=(cst.LeftParen(whitespace_after=cst.SimpleWhitespace(" ")),),
                    value=cst.Integer("5"),
                    whitespace_after_yield=cst.SimpleWhitespace("  "),
                    rpar=(cst.RightParen(whitespace_before=cst.SimpleWhitespace(" ")),),
                ),
                "( yield  5 )",
            ),
            (
                cst.Yield(
                    lpar=(cst.LeftParen(whitespace_after=cst.SimpleWhitespace(" ")),),
                    value=cst.From(
                        cst.Call(cst.Name("bla")),
                        whitespace_after_from=cst.SimpleWhitespace("  "),
                    ),
                    whitespace_after_yield=cst.SimpleWhitespace("  "),
                    rpar=(cst.RightParen(whitespace_before=cst.SimpleWhitespace(" ")),),
                ),
                "( yield  from  bla() )",
            ),
        )
    )
    def test_valid(
        self, node: cst.CSTNode, code: str, position: Optional[CodeRange] = None
    ) -> None:
        self.validate_node(
            node,
            code,
            lambda code: ensure_type(
                ensure_type(parse_statement(code), cst.SimpleStatementLine).body[0],
                cst.Expr,
            ).value,
        )

    @data_provider(
        (
            {
                "code": "yield from x",
                "parser": parse_statement_as(python_version="3.3"),
                "expect_success": True,
            },
            {
                "code": "yield from x",
                "parser": parse_statement_as(python_version="3.1"),
                "expect_success": False,
            },
        )
    )
    def test_versions(self, **kwargs: Any) -> None:
        if is_native() and not kwargs.get("expect_success", True):
            self.skipTest("parse errors are disabled for native parser")
        self.assert_parses(**kwargs)
