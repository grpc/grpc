# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any

import libcst as cst
from libcst import parse_statement, PartialParserConfig
from libcst._nodes.tests.base import CSTNodeTest, DummyIndentedBlock
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider


class ForTest(CSTNodeTest):
    @data_provider(
        (
            # Simple for block
            {
                "node": cst.For(
                    cst.Name("target"),
                    cst.Call(cst.Name("iter")),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "for target in iter(): pass\n",
                "parser": parse_statement,
            },
            # Simple async for block
            {
                "node": cst.For(
                    cst.Name("target"),
                    cst.Call(cst.Name("iter")),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    asynchronous=cst.Asynchronous(),
                ),
                "code": "async for target in iter(): pass\n",
                "parser": lambda code: parse_statement(
                    code, config=PartialParserConfig(python_version="3.7")
                ),
            },
            # Python 3.6 async for block
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.IndentedBlock(
                        (
                            cst.For(
                                cst.Name("target"),
                                cst.Call(cst.Name("iter")),
                                cst.SimpleStatementSuite((cst.Pass(),)),
                                asynchronous=cst.Asynchronous(),
                            ),
                        )
                    ),
                    asynchronous=cst.Asynchronous(),
                ),
                "code": "async def foo():\n    async for target in iter(): pass\n",
                "parser": lambda code: parse_statement(
                    code, config=PartialParserConfig(python_version="3.6")
                ),
            },
            # For block with else
            {
                "node": cst.For(
                    cst.Name("target"),
                    cst.Call(cst.Name("iter")),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    cst.Else(cst.SimpleStatementSuite((cst.Pass(),))),
                ),
                "code": "for target in iter(): pass\nelse: pass\n",
                "parser": parse_statement,
            },
            # indentation
            {
                "node": DummyIndentedBlock(
                    "    ",
                    cst.For(
                        cst.Name("target"),
                        cst.Call(cst.Name("iter")),
                        cst.SimpleStatementSuite((cst.Pass(),)),
                    ),
                ),
                "code": "    for target in iter(): pass\n",
                "parser": None,
            },
            # for an indented body
            {
                "node": DummyIndentedBlock(
                    "    ",
                    cst.For(
                        cst.Name("target"),
                        cst.Call(cst.Name("iter")),
                        cst.IndentedBlock((cst.SimpleStatementLine((cst.Pass(),)),)),
                    ),
                ),
                "code": "    for target in iter():\n        pass\n",
                "parser": None,
                "expected_position": CodeRange((1, 4), (2, 12)),
            },
            # leading_lines
            {
                "node": cst.For(
                    cst.Name("target"),
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
                "code": "# leading comment\nfor target in iter():\n    pass\n# else comment\nelse:\n    pass\n",
                "parser": None,
                "expected_position": CodeRange((2, 0), (6, 8)),
            },
            # Weird spacing rules
            {
                "node": cst.For(
                    cst.Name(
                        "target", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                    ),
                    cst.Call(
                        cst.Name("iter"),
                        lpar=(cst.LeftParen(),),
                        rpar=(cst.RightParen(),),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    whitespace_after_for=cst.SimpleWhitespace(""),
                    whitespace_before_in=cst.SimpleWhitespace(""),
                    whitespace_after_in=cst.SimpleWhitespace(""),
                ),
                "code": "for(target)in(iter()): pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (1, 27)),
            },
            # Whitespace
            {
                "node": cst.For(
                    cst.Name("target"),
                    cst.Call(cst.Name("iter")),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    whitespace_after_for=cst.SimpleWhitespace("  "),
                    whitespace_before_in=cst.SimpleWhitespace("  "),
                    whitespace_after_in=cst.SimpleWhitespace("  "),
                    whitespace_before_colon=cst.SimpleWhitespace("  "),
                ),
                "code": "for  target  in  iter()  : pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (1, 31)),
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            {
                "get_node": lambda: cst.For(
                    cst.Name("target"),
                    cst.Call(cst.Name("iter")),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    whitespace_after_for=cst.SimpleWhitespace(""),
                ),
                "expected_re": "Must have at least one space after 'for' keyword",
            },
            {
                "get_node": lambda: cst.For(
                    cst.Name("target"),
                    cst.Call(cst.Name("iter")),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    whitespace_before_in=cst.SimpleWhitespace(""),
                ),
                "expected_re": "Must have at least one space before 'in' keyword",
            },
            {
                "get_node": lambda: cst.For(
                    cst.Name("target"),
                    cst.Call(cst.Name("iter")),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    whitespace_after_in=cst.SimpleWhitespace(""),
                ),
                "expected_re": "Must have at least one space after 'in' keyword",
            },
        )
    )
    def test_invalid(self, **kwargs: Any) -> None:
        self.assert_invalid(**kwargs)
