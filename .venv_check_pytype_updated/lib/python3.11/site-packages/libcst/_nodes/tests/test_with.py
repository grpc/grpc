# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any

import libcst as cst
from libcst import parse_statement, PartialParserConfig
from libcst._maybe_sentinel import MaybeSentinel
from libcst._nodes.tests.base import CSTNodeTest, DummyIndentedBlock, parse_statement_as
from libcst._parser.entrypoints import is_native
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider


class WithTest(CSTNodeTest):
    maxDiff: int = 2000

    @data_provider(
        (
            # Simple with block
            {
                "node": cst.With(
                    (cst.WithItem(cst.Call(cst.Name("context_mgr"))),),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "with context_mgr(): pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (1, 24)),
            },
            # Simple async with block
            {
                "node": cst.With(
                    (cst.WithItem(cst.Call(cst.Name("context_mgr"))),),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    asynchronous=cst.Asynchronous(),
                ),
                "code": "async with context_mgr(): pass\n",
                "parser": lambda code: parse_statement(
                    code, config=PartialParserConfig(python_version="3.7")
                ),
            },
            # Python 3.6 async with block
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.IndentedBlock(
                        (
                            cst.With(
                                (cst.WithItem(cst.Call(cst.Name("context_mgr"))),),
                                cst.SimpleStatementSuite((cst.Pass(),)),
                                asynchronous=cst.Asynchronous(),
                            ),
                        )
                    ),
                    asynchronous=cst.Asynchronous(),
                ),
                "code": "async def foo():\n    async with context_mgr(): pass\n",
                "parser": lambda code: parse_statement(
                    code, config=PartialParserConfig(python_version="3.6")
                ),
            },
            # Multiple context managers
            {
                "node": cst.With(
                    (
                        cst.WithItem(cst.Call(cst.Name("foo"))),
                        cst.WithItem(cst.Call(cst.Name("bar"))),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "with foo(), bar(): pass\n",
                "parser": None,
            },
            {
                "node": cst.With(
                    (
                        cst.WithItem(
                            cst.Call(cst.Name("foo")),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.WithItem(cst.Call(cst.Name("bar"))),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "with foo(), bar(): pass\n",
                "parser": parse_statement,
            },
            # With block containing variable for context manager.
            {
                "node": cst.With(
                    (
                        cst.WithItem(
                            cst.Call(cst.Name("context_mgr")),
                            cst.AsName(cst.Name("ctx")),
                        ),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "with context_mgr() as ctx: pass\n",
                "parser": parse_statement,
            },
            {
                "node": cst.With(
                    (
                        cst.WithItem(
                            cst.Call(cst.Name("context_mgr")),
                            cst.AsName(
                                cst.Tuple(()),
                                whitespace_after_as=cst.SimpleWhitespace(""),
                                whitespace_before_as=cst.SimpleWhitespace(""),
                            ),
                        ),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "with context_mgr()as(): pass\n",
                "parser": parse_statement,
            },
            # indentation
            {
                "node": DummyIndentedBlock(
                    "    ",
                    cst.With(
                        (cst.WithItem(cst.Call(cst.Name("context_mgr"))),),
                        cst.SimpleStatementSuite((cst.Pass(),)),
                    ),
                ),
                "code": "    with context_mgr(): pass\n",
                "parser": None,
                "expected_position": CodeRange((1, 4), (1, 28)),
            },
            # with an indented body
            {
                "node": DummyIndentedBlock(
                    "    ",
                    cst.With(
                        (cst.WithItem(cst.Call(cst.Name("context_mgr"))),),
                        cst.IndentedBlock((cst.SimpleStatementLine((cst.Pass(),)),)),
                    ),
                ),
                "code": "    with context_mgr():\n        pass\n",
                "parser": None,
                "expected_position": CodeRange((1, 4), (2, 12)),
            },
            # leading_lines
            {
                "node": cst.With(
                    (cst.WithItem(cst.Call(cst.Name("context_mgr"))),),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    leading_lines=(
                        cst.EmptyLine(comment=cst.Comment("# leading comment")),
                    ),
                ),
                "code": "# leading comment\nwith context_mgr(): pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((2, 0), (2, 24)),
            },
            # Whitespace
            {
                "node": cst.With(
                    (
                        cst.WithItem(
                            cst.Call(cst.Name("context_mgr")),
                            cst.AsName(
                                cst.Name("ctx"),
                                whitespace_before_as=cst.SimpleWhitespace("  "),
                                whitespace_after_as=cst.SimpleWhitespace("  "),
                            ),
                        ),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    whitespace_after_with=cst.SimpleWhitespace("  "),
                    whitespace_before_colon=cst.SimpleWhitespace("  "),
                ),
                "code": "with  context_mgr()  as  ctx  : pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (1, 36)),
            },
            # Weird spacing rules, that parse differently depending on whether
            # we are using a grammar that included parenthesized with statements.
            {
                "node": cst.With(
                    (
                        cst.WithItem(
                            cst.Call(
                                cst.Name("context_mgr"),
                                lpar=() if is_native() else (cst.LeftParen(),),
                                rpar=() if is_native() else (cst.RightParen(),),
                            )
                        ),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    lpar=(cst.LeftParen() if is_native() else MaybeSentinel.DEFAULT),
                    rpar=(cst.RightParen() if is_native() else MaybeSentinel.DEFAULT),
                    whitespace_after_with=cst.SimpleWhitespace(""),
                ),
                "code": "with(context_mgr()): pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (1, 25)),
            },
            # Multi-line parenthesized with.
            {
                "node": cst.With(
                    (
                        cst.WithItem(
                            cst.Call(cst.Name("foo")),
                            comma=cst.Comma(
                                whitespace_after=cst.ParenthesizedWhitespace(
                                    first_line=cst.TrailingWhitespace(
                                        whitespace=cst.SimpleWhitespace(
                                            value="",
                                        ),
                                        comment=None,
                                        newline=cst.Newline(
                                            value=None,
                                        ),
                                    ),
                                    empty_lines=[],
                                    indent=True,
                                    last_line=cst.SimpleWhitespace(
                                        value="       ",
                                    ),
                                )
                            ),
                        ),
                        cst.WithItem(cst.Call(cst.Name("bar")), comma=cst.Comma()),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    lpar=cst.LeftParen(whitespace_after=cst.SimpleWhitespace(" ")),
                    rpar=cst.RightParen(whitespace_before=cst.SimpleWhitespace(" ")),
                ),
                "code": ("with ( foo(),\n" "       bar(), ): pass\n"),  # noqa
                "parser": parse_statement if is_native() else None,
                "expected_position": CodeRange((1, 0), (2, 21)),
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            {
                "get_node": lambda: cst.With(
                    (), cst.IndentedBlock((cst.SimpleStatementLine((cst.Pass(),)),))
                ),
                "expected_re": "A With statement must have at least one WithItem",
            },
            {
                "get_node": lambda: cst.With(
                    (
                        cst.WithItem(
                            cst.Call(cst.Name("foo")),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                    ),
                    cst.IndentedBlock((cst.SimpleStatementLine((cst.Pass(),)),)),
                ),
                "expected_re": "The last WithItem in an unparenthesized With cannot "
                + "have a trailing comma.",
            },
            {
                "get_node": lambda: cst.With(
                    (cst.WithItem(cst.Call(cst.Name("context_mgr"))),),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    whitespace_after_with=cst.SimpleWhitespace(""),
                ),
                "expected_re": "Must have at least one space after with keyword",
            },
            {
                "get_node": lambda: cst.With(
                    (cst.WithItem(cst.Call(cst.Name("context_mgr"))),),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    whitespace_after_with=cst.SimpleWhitespace(""),
                    lpar=cst.LeftParen(),
                ),
                "expected_re": "Do not mix concrete LeftParen/RightParen with "
                + "MaybeSentinel",
            },
            {
                "get_node": lambda: cst.With(
                    (cst.WithItem(cst.Call(cst.Name("context_mgr"))),),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    whitespace_after_with=cst.SimpleWhitespace(""),
                    rpar=cst.RightParen(),
                ),
                "expected_re": "Do not mix concrete LeftParen/RightParen with "
                + "MaybeSentinel",
            },
        )
    )
    def test_invalid(self, **kwargs: Any) -> None:
        self.assert_invalid(**kwargs)

    @data_provider(
        (
            {
                "code": "with a, b: pass",
                "parser": parse_statement_as(python_version="3.1"),
                "expect_success": True,
            },
            {
                "code": "with a, b: pass",
                "parser": parse_statement_as(python_version="3.0"),
                "expect_success": False,
            },
        )
    )
    def test_versions(self, **kwargs: Any) -> None:
        if is_native() and not kwargs.get("expect_success", True):
            self.skipTest("parse errors are disabled for native parser")
        self.assert_parses(**kwargs)

    def test_adding_parens(self) -> None:
        node = cst.With(
            (
                cst.WithItem(
                    cst.Call(cst.Name("foo")),
                    comma=cst.Comma(
                        whitespace_after=cst.ParenthesizedWhitespace(),
                    ),
                ),
                cst.WithItem(cst.Call(cst.Name("bar")), comma=cst.Comma()),
            ),
            cst.SimpleStatementSuite((cst.Pass(),)),
            lpar=cst.LeftParen(whitespace_after=cst.SimpleWhitespace(" ")),
            rpar=cst.RightParen(whitespace_before=cst.SimpleWhitespace(" ")),
        )
        module = cst.Module([])
        self.assertEqual(
            module.code_for_node(node), ("with ( foo(),\n" "bar(), ): pass\n")  # noqa
        )
