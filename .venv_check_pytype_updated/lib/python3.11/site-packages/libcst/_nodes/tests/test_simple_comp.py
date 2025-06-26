# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any, Callable

import libcst as cst
from libcst import parse_expression, parse_statement, PartialParserConfig
from libcst._nodes.tests.base import CSTNodeTest
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider


class SimpleCompTest(CSTNodeTest):
    @data_provider(
        [
            # simple GeneratorExp
            {
                "node": cst.GeneratorExp(
                    cst.Name("a"), cst.CompFor(target=cst.Name("b"), iter=cst.Name("c"))
                ),
                "code": "(a for b in c)",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 1), (1, 13)),
            },
            # simple ListComp
            {
                "node": cst.ListComp(
                    cst.Name("a"), cst.CompFor(target=cst.Name("b"), iter=cst.Name("c"))
                ),
                "code": "[a for b in c]",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 0), (1, 14)),
            },
            # simple SetComp
            {
                "node": cst.SetComp(
                    cst.Name("a"), cst.CompFor(target=cst.Name("b"), iter=cst.Name("c"))
                ),
                "code": "{a for b in c}",
                "parser": parse_expression,
            },
            # non-trivial elt in GeneratorExp
            {
                "node": cst.GeneratorExp(
                    cst.BinaryOperation(cst.Name("a1"), cst.Add(), cst.Name("a2")),
                    cst.CompFor(target=cst.Name("b"), iter=cst.Name("c")),
                ),
                "code": "(a1 + a2 for b in c)",
                "parser": parse_expression,
            },
            # non-trivial elt in ListComp
            {
                "node": cst.ListComp(
                    cst.BinaryOperation(cst.Name("a1"), cst.Add(), cst.Name("a2")),
                    cst.CompFor(target=cst.Name("b"), iter=cst.Name("c")),
                ),
                "code": "[a1 + a2 for b in c]",
                "parser": parse_expression,
            },
            # non-trivial elt in SetComp
            {
                "node": cst.SetComp(
                    cst.BinaryOperation(cst.Name("a1"), cst.Add(), cst.Name("a2")),
                    cst.CompFor(target=cst.Name("b"), iter=cst.Name("c")),
                ),
                "code": "{a1 + a2 for b in c}",
                "parser": parse_expression,
            },
            # async GeneratorExp
            {
                "node": cst.GeneratorExp(
                    cst.Name("a"),
                    cst.CompFor(
                        target=cst.Name("b"),
                        iter=cst.Name("c"),
                        asynchronous=cst.Asynchronous(),
                    ),
                ),
                "code": "(a async for b in c)",
                "parser": lambda code: parse_expression(
                    code, config=PartialParserConfig(python_version="3.7")
                ),
            },
            # Python 3.6 async GeneratorExp
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.IndentedBlock(
                        (
                            cst.SimpleStatementLine(
                                (
                                    cst.Expr(
                                        cst.GeneratorExp(
                                            cst.Name("a"),
                                            cst.CompFor(
                                                target=cst.Name("b"),
                                                iter=cst.Name("c"),
                                                asynchronous=cst.Asynchronous(),
                                            ),
                                        )
                                    ),
                                )
                            ),
                        )
                    ),
                    asynchronous=cst.Asynchronous(),
                ),
                "code": "async def foo():\n    (a async for b in c)\n",
                "parser": lambda code: parse_statement(
                    code, config=PartialParserConfig(python_version="3.6")
                ),
            },
            # a generator doesn't have to own it's own parenthesis
            {
                "node": cst.Call(
                    cst.Name("func"),
                    [
                        cst.Arg(
                            cst.GeneratorExp(
                                cst.Name("a"),
                                cst.CompFor(target=cst.Name("b"), iter=cst.Name("c")),
                                lpar=[],
                                rpar=[],
                            )
                        )
                    ],
                ),
                "code": "func(a for b in c)",
                "parser": parse_expression,
            },
            # add a few 'if' clauses
            {
                "node": cst.GeneratorExp(
                    cst.Name("a"),
                    cst.CompFor(
                        target=cst.Name("b"),
                        iter=cst.Name("c"),
                        ifs=[
                            cst.CompIf(cst.Name("d")),
                            cst.CompIf(cst.Name("e")),
                            cst.CompIf(cst.Name("f")),
                        ],
                    ),
                ),
                "code": "(a for b in c if d if e if f)",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 1), (1, 28)),
            },
            # nested/inner for-in clause
            {
                "node": cst.GeneratorExp(
                    cst.Name("a"),
                    cst.CompFor(
                        target=cst.Name("b"),
                        iter=cst.Name("c"),
                        inner_for_in=cst.CompFor(
                            target=cst.Name("d"), iter=cst.Name("e")
                        ),
                    ),
                ),
                "code": "(a for b in c for d in e)",
                "parser": parse_expression,
            },
            # nested/inner for-in clause with an 'if' clause
            {
                "node": cst.GeneratorExp(
                    cst.Name("a"),
                    cst.CompFor(
                        target=cst.Name("b"),
                        iter=cst.Name("c"),
                        ifs=[cst.CompIf(cst.Name("d"))],
                        inner_for_in=cst.CompFor(
                            target=cst.Name("e"), iter=cst.Name("f")
                        ),
                    ),
                ),
                "code": "(a for b in c if d for e in f)",
                "parser": parse_expression,
            },
            # custom whitespace
            {
                "node": cst.GeneratorExp(
                    cst.Name("a"),
                    cst.CompFor(
                        target=cst.Name("b"),
                        iter=cst.Name("c"),
                        ifs=[
                            cst.CompIf(
                                cst.Name("d"),
                                whitespace_before=cst.SimpleWhitespace("\t"),
                                whitespace_before_test=cst.SimpleWhitespace("\t\t"),
                            )
                        ],
                        whitespace_before=cst.SimpleWhitespace("  "),
                        whitespace_after_for=cst.SimpleWhitespace("   "),
                        whitespace_before_in=cst.SimpleWhitespace("    "),
                        whitespace_after_in=cst.SimpleWhitespace("     "),
                    ),
                    lpar=[cst.LeftParen(whitespace_after=cst.SimpleWhitespace("\f"))],
                    rpar=[
                        cst.RightParen(whitespace_before=cst.SimpleWhitespace("\f\f"))
                    ],
                ),
                "code": "(\fa  for   b    in     c\tif\t\td\f\f)",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 2), (1, 30)),
            },
            # custom whitespace around ListComp's brackets
            {
                "node": cst.ListComp(
                    cst.Name("a"),
                    cst.CompFor(target=cst.Name("b"), iter=cst.Name("c")),
                    lbracket=cst.LeftSquareBracket(
                        whitespace_after=cst.SimpleWhitespace("\t")
                    ),
                    rbracket=cst.RightSquareBracket(
                        whitespace_before=cst.SimpleWhitespace("\t\t")
                    ),
                    lpar=[cst.LeftParen(whitespace_after=cst.SimpleWhitespace("\f"))],
                    rpar=[
                        cst.RightParen(whitespace_before=cst.SimpleWhitespace("\f\f"))
                    ],
                ),
                "code": "(\f[\ta for b in c\t\t]\f\f)",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 2), (1, 19)),
            },
            # custom whitespace around SetComp's braces
            {
                "node": cst.SetComp(
                    cst.Name("a"),
                    cst.CompFor(target=cst.Name("b"), iter=cst.Name("c")),
                    lbrace=cst.LeftCurlyBrace(
                        whitespace_after=cst.SimpleWhitespace("\t")
                    ),
                    rbrace=cst.RightCurlyBrace(
                        whitespace_before=cst.SimpleWhitespace("\t\t")
                    ),
                    lpar=[cst.LeftParen(whitespace_after=cst.SimpleWhitespace("\f"))],
                    rpar=[
                        cst.RightParen(whitespace_before=cst.SimpleWhitespace("\f\f"))
                    ],
                ),
                "code": "(\f{\ta for b in c\t\t}\f\f)",
                "parser": parse_expression,
            },
            # no whitespace between elements
            {
                "node": cst.GeneratorExp(
                    cst.Name("a", lpar=[cst.LeftParen()], rpar=[cst.RightParen()]),
                    cst.CompFor(
                        target=cst.Name(
                            "b", lpar=[cst.LeftParen()], rpar=[cst.RightParen()]
                        ),
                        iter=cst.Name(
                            "c", lpar=[cst.LeftParen()], rpar=[cst.RightParen()]
                        ),
                        ifs=[
                            cst.CompIf(
                                cst.Name(
                                    "d", lpar=[cst.LeftParen()], rpar=[cst.RightParen()]
                                ),
                                whitespace_before=cst.SimpleWhitespace(""),
                                whitespace_before_test=cst.SimpleWhitespace(""),
                            )
                        ],
                        inner_for_in=cst.CompFor(
                            target=cst.Name(
                                "e", lpar=[cst.LeftParen()], rpar=[cst.RightParen()]
                            ),
                            iter=cst.Name(
                                "f", lpar=[cst.LeftParen()], rpar=[cst.RightParen()]
                            ),
                            whitespace_before=cst.SimpleWhitespace(""),
                            whitespace_after_for=cst.SimpleWhitespace(""),
                            whitespace_before_in=cst.SimpleWhitespace(""),
                            whitespace_after_in=cst.SimpleWhitespace(""),
                        ),
                        whitespace_before=cst.SimpleWhitespace(""),
                        whitespace_after_for=cst.SimpleWhitespace(""),
                        whitespace_before_in=cst.SimpleWhitespace(""),
                        whitespace_after_in=cst.SimpleWhitespace(""),
                    ),
                    lpar=[cst.LeftParen()],
                    rpar=[cst.RightParen()],
                ),
                "code": "((a)for(b)in(c)if(d)for(e)in(f))",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 1), (1, 31)),
            },
            # no whitespace before/after GeneratorExp is valid
            {
                "node": cst.Comparison(
                    cst.GeneratorExp(
                        cst.Name("a"),
                        cst.CompFor(target=cst.Name("b"), iter=cst.Name("c")),
                    ),
                    [
                        cst.ComparisonTarget(
                            cst.Is(
                                whitespace_before=cst.SimpleWhitespace(""),
                                whitespace_after=cst.SimpleWhitespace(""),
                            ),
                            cst.GeneratorExp(
                                cst.Name("d"),
                                cst.CompFor(target=cst.Name("e"), iter=cst.Name("f")),
                            ),
                        )
                    ],
                ),
                "code": "(a for b in c)is(d for e in f)",
                "parser": parse_expression,
            },
            # no whitespace before/after ListComp is valid
            {
                "node": cst.Comparison(
                    cst.ListComp(
                        cst.Name("a"),
                        cst.CompFor(target=cst.Name("b"), iter=cst.Name("c")),
                    ),
                    [
                        cst.ComparisonTarget(
                            cst.Is(
                                whitespace_before=cst.SimpleWhitespace(""),
                                whitespace_after=cst.SimpleWhitespace(""),
                            ),
                            cst.ListComp(
                                cst.Name("d"),
                                cst.CompFor(target=cst.Name("e"), iter=cst.Name("f")),
                            ),
                        )
                    ],
                ),
                "code": "[a for b in c]is[d for e in f]",
                "parser": parse_expression,
            },
            # no whitespace before/after SetComp is valid
            {
                "node": cst.Comparison(
                    cst.SetComp(
                        cst.Name("a"),
                        cst.CompFor(target=cst.Name("b"), iter=cst.Name("c")),
                    ),
                    [
                        cst.ComparisonTarget(
                            cst.Is(
                                whitespace_before=cst.SimpleWhitespace(""),
                                whitespace_after=cst.SimpleWhitespace(""),
                            ),
                            cst.SetComp(
                                cst.Name("d"),
                                cst.CompFor(target=cst.Name("e"), iter=cst.Name("f")),
                            ),
                        )
                    ],
                ),
                "code": "{a for b in c}is{d for e in f}",
                "parser": parse_expression,
            },
        ]
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            (
                lambda: cst.GeneratorExp(
                    cst.Name("a"),
                    cst.CompFor(target=cst.Name("b"), iter=cst.Name("c")),
                    lpar=[cst.LeftParen(), cst.LeftParen()],
                    rpar=[cst.RightParen()],
                ),
                "unbalanced parens",
            ),
            (
                lambda: cst.ListComp(
                    cst.Name("a"),
                    cst.CompFor(target=cst.Name("b"), iter=cst.Name("c")),
                    lpar=[cst.LeftParen(), cst.LeftParen()],
                    rpar=[cst.RightParen()],
                ),
                "unbalanced parens",
            ),
            (
                lambda: cst.SetComp(
                    cst.Name("a"),
                    cst.CompFor(target=cst.Name("b"), iter=cst.Name("c")),
                    lpar=[cst.LeftParen(), cst.LeftParen()],
                    rpar=[cst.RightParen()],
                ),
                "unbalanced parens",
            ),
            (
                lambda: cst.GeneratorExp(
                    cst.Name("a"),
                    cst.CompFor(
                        target=cst.Name("b"),
                        iter=cst.Name("c"),
                        whitespace_before=cst.SimpleWhitespace(""),
                    ),
                ),
                "Must have at least one space before 'for' keyword.",
            ),
            (
                lambda: cst.GeneratorExp(
                    cst.Name("a"),
                    cst.CompFor(
                        target=cst.Name("b"),
                        iter=cst.Name("c"),
                        asynchronous=cst.Asynchronous(),
                        whitespace_before=cst.SimpleWhitespace(""),
                    ),
                ),
                "Must have at least one space before 'async' keyword.",
            ),
            (
                lambda: cst.GeneratorExp(
                    cst.Name("a"),
                    cst.CompFor(
                        target=cst.Name("b"),
                        iter=cst.Name("c"),
                        whitespace_after_for=cst.SimpleWhitespace(""),
                    ),
                ),
                "Must have at least one space after 'for' keyword.",
            ),
            (
                lambda: cst.GeneratorExp(
                    cst.Name("a"),
                    cst.CompFor(
                        target=cst.Name("b"),
                        iter=cst.Name("c"),
                        whitespace_before_in=cst.SimpleWhitespace(""),
                    ),
                ),
                "Must have at least one space before 'in' keyword.",
            ),
            (
                lambda: cst.GeneratorExp(
                    cst.Name("a"),
                    cst.CompFor(
                        target=cst.Name("b"),
                        iter=cst.Name("c"),
                        whitespace_after_in=cst.SimpleWhitespace(""),
                    ),
                ),
                "Must have at least one space after 'in' keyword.",
            ),
            (
                lambda: cst.GeneratorExp(
                    cst.Name("a"),
                    cst.CompFor(
                        target=cst.Name("b"),
                        iter=cst.Name("c"),
                        ifs=[
                            cst.CompIf(
                                cst.Name("d"),
                                whitespace_before=cst.SimpleWhitespace(""),
                            )
                        ],
                    ),
                ),
                "Must have at least one space before 'if' keyword.",
            ),
            (
                lambda: cst.GeneratorExp(
                    cst.Name("a"),
                    cst.CompFor(
                        target=cst.Name("b"),
                        iter=cst.Name("c"),
                        ifs=[
                            cst.CompIf(
                                cst.Name("d"),
                                whitespace_before_test=cst.SimpleWhitespace(""),
                            )
                        ],
                    ),
                ),
                "Must have at least one space after 'if' keyword.",
            ),
            (
                lambda: cst.GeneratorExp(
                    cst.Name("a"),
                    cst.CompFor(
                        target=cst.Name("b"),
                        iter=cst.Name("c"),
                        inner_for_in=cst.CompFor(
                            target=cst.Name("d"),
                            iter=cst.Name("e"),
                            whitespace_before=cst.SimpleWhitespace(""),
                        ),
                    ),
                ),
                "Must have at least one space before 'for' keyword.",
            ),
            (
                lambda: cst.GeneratorExp(
                    cst.Name("a"),
                    cst.CompFor(
                        target=cst.Name("b"),
                        iter=cst.Name("c"),
                        inner_for_in=cst.CompFor(
                            target=cst.Name("d"),
                            iter=cst.Name("e"),
                            asynchronous=cst.Asynchronous(),
                            whitespace_before=cst.SimpleWhitespace(""),
                        ),
                    ),
                ),
                "Must have at least one space before 'async' keyword.",
            ),
        )
    )
    def test_invalid(
        self, get_node: Callable[[], cst.CSTNode], expected_re: str
    ) -> None:
        self.assert_invalid(get_node, expected_re)
