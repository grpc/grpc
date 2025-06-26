# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any

import libcst as cst
from libcst._nodes.tests.base import CSTNodeTest
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider


def _parse_expression_force_38(code: str) -> cst.BaseExpression:
    return cst.parse_expression(
        code, config=cst.PartialParserConfig(python_version="3.8")
    )


def _parse_statement_force_38(code: str) -> cst.BaseCompoundStatement:
    statement = cst.parse_statement(
        code, config=cst.PartialParserConfig(python_version="3.8")
    )
    if not isinstance(statement, cst.BaseCompoundStatement):
        raise ValueError(
            "This function is expecting to parse compound statements only!"
        )
    return statement


class NamedExprTest(CSTNodeTest):
    @data_provider(
        (
            # Simple named expression
            {
                "node": cst.NamedExpr(cst.Name("x"), cst.Float("5.5")),
                "code": "x := 5.5",
                "parser": None,  # Walrus operator is illegal as top-level statement
                "expected_position": None,
            },
            # Parenthesized named expression
            {
                "node": cst.NamedExpr(
                    lpar=(cst.LeftParen(),),
                    target=cst.Name("foo"),
                    value=cst.Integer("5"),
                    rpar=(cst.RightParen(),),
                ),
                "code": "(foo := 5)",
                "parser": _parse_expression_force_38,
                "expected_position": CodeRange((1, 1), (1, 9)),
            },
            # Make sure that spacing works
            {
                "node": cst.NamedExpr(
                    lpar=(cst.LeftParen(whitespace_after=cst.SimpleWhitespace(" ")),),
                    target=cst.Name("foo"),
                    whitespace_before_walrus=cst.SimpleWhitespace("  "),
                    whitespace_after_walrus=cst.SimpleWhitespace("  "),
                    value=cst.Name("bar"),
                    rpar=(cst.RightParen(whitespace_before=cst.SimpleWhitespace(" ")),),
                ),
                "code": "( foo  :=  bar )",
                "parser": _parse_expression_force_38,
                "expected_position": CodeRange((1, 2), (1, 14)),
            },
            # Make sure we can use these where allowed in if/while statements
            {
                "node": cst.While(
                    test=cst.NamedExpr(
                        target=cst.Name(value="x"),
                        value=cst.Call(func=cst.Name(value="some_input")),
                    ),
                    body=cst.SimpleStatementSuite(body=[cst.Pass()]),
                ),
                "code": "while x := some_input(): pass\n",
                "parser": _parse_statement_force_38,
                "expected_position": None,
            },
            {
                "node": cst.If(
                    test=cst.NamedExpr(
                        target=cst.Name(value="x"),
                        value=cst.Call(func=cst.Name(value="some_input")),
                    ),
                    body=cst.SimpleStatementSuite(body=[cst.Pass()]),
                ),
                "code": "if x := some_input(): pass\n",
                "parser": _parse_statement_force_38,
                "expected_position": None,
            },
            {
                "node": cst.If(
                    test=cst.NamedExpr(
                        target=cst.Name(value="x"),
                        value=cst.Integer(value="1"),
                        whitespace_before_walrus=cst.SimpleWhitespace(""),
                        whitespace_after_walrus=cst.SimpleWhitespace(""),
                    ),
                    body=cst.SimpleStatementSuite(body=[cst.Pass()]),
                ),
                "code": "if x:=1: pass\n",
                "parser": _parse_statement_force_38,
                "expected_position": None,
            },
            # Function args
            {
                "node": cst.Call(
                    func=cst.Name(value="f"),
                    args=[
                        cst.Arg(
                            value=cst.NamedExpr(
                                target=cst.Name(value="y"),
                                value=cst.Integer(value="1"),
                                whitespace_before_walrus=cst.SimpleWhitespace(""),
                                whitespace_after_walrus=cst.SimpleWhitespace(""),
                            )
                        ),
                    ],
                ),
                "code": "f(y:=1)",
                "parser": _parse_expression_force_38,
                "expected_position": None,
            },
            # Whitespace handling on args is fragile
            {
                "node": cst.Call(
                    func=cst.Name(value="f"),
                    args=[
                        cst.Arg(
                            value=cst.Name(value="x"),
                            comma=cst.Comma(
                                whitespace_after=cst.SimpleWhitespace("  ")
                            ),
                        ),
                        cst.Arg(
                            value=cst.NamedExpr(
                                target=cst.Name(value="y"),
                                value=cst.Integer(value="1"),
                                whitespace_before_walrus=cst.SimpleWhitespace("   "),
                                whitespace_after_walrus=cst.SimpleWhitespace("    "),
                            ),
                            whitespace_after_arg=cst.SimpleWhitespace("     "),
                        ),
                    ],
                ),
                "code": "f(x,  y   :=    1     )",
                "parser": _parse_expression_force_38,
                "expected_position": None,
            },
            {
                "node": cst.Call(
                    func=cst.Name(value="f"),
                    args=[
                        cst.Arg(
                            value=cst.NamedExpr(
                                target=cst.Name(value="y"),
                                value=cst.Integer(value="1"),
                                whitespace_before_walrus=cst.SimpleWhitespace("   "),
                                whitespace_after_walrus=cst.SimpleWhitespace("    "),
                            ),
                            whitespace_after_arg=cst.SimpleWhitespace("     "),
                        ),
                    ],
                    whitespace_before_args=cst.SimpleWhitespace("  "),
                ),
                "code": "f(  y   :=    1     )",
                "parser": _parse_expression_force_38,
                "expected_position": None,
            },
            {
                "node": cst.ListComp(
                    elt=cst.NamedExpr(
                        cst.Name("_"),
                        cst.SimpleString("''"),
                        whitespace_after_walrus=cst.SimpleWhitespace(""),
                        whitespace_before_walrus=cst.SimpleWhitespace(""),
                    ),
                    for_in=cst.CompFor(
                        target=cst.Name("_"),
                        iter=cst.Name("_"),
                        whitespace_before=cst.SimpleWhitespace(""),
                    ),
                ),
                "code": "[_:=''for _ in _]",
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            {
                "get_node": (
                    lambda: cst.NamedExpr(
                        cst.Name("foo"), cst.Name("bar"), lpar=(cst.LeftParen(),)
                    )
                ),
                "expected_re": "left paren without right paren",
            },
            {
                "get_node": (
                    lambda: cst.NamedExpr(
                        cst.Name("foo"), cst.Name("bar"), rpar=(cst.RightParen(),)
                    )
                ),
                "expected_re": "right paren without left paren",
            },
        )
    )
    def test_invalid(self, **kwargs: Any) -> None:
        self.assert_invalid(**kwargs)
