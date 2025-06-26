# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Callable, Optional

import libcst as cst
from libcst import parse_expression
from libcst._nodes.tests.base import CSTNodeTest
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider


class ComparisonTest(CSTNodeTest):
    @data_provider(
        (
            # Simple comparison statements
            (
                cst.Comparison(
                    cst.Name("foo"),
                    (cst.ComparisonTarget(cst.LessThan(), cst.Integer("5")),),
                ),
                "foo < 5",
            ),
            (
                cst.Comparison(
                    cst.Name("foo"),
                    (cst.ComparisonTarget(cst.NotEqual(), cst.Integer("5")),),
                ),
                "foo != 5",
            ),
            (
                cst.Comparison(
                    cst.Name("foo"), (cst.ComparisonTarget(cst.Is(), cst.Name("True")),)
                ),
                "foo is True",
            ),
            (
                cst.Comparison(
                    cst.Name("foo"),
                    (cst.ComparisonTarget(cst.IsNot(), cst.Name("False")),),
                ),
                "foo is not False",
            ),
            (
                cst.Comparison(
                    cst.Name("foo"), (cst.ComparisonTarget(cst.In(), cst.Name("bar")),)
                ),
                "foo in bar",
            ),
            (
                cst.Comparison(
                    cst.Name("foo"),
                    (cst.ComparisonTarget(cst.NotIn(), cst.Name("bar")),),
                ),
                "foo not in bar",
            ),
            # Comparison with parens
            (
                cst.Comparison(
                    lpar=(cst.LeftParen(),),
                    left=cst.Name("foo"),
                    comparisons=(
                        cst.ComparisonTarget(
                            operator=cst.NotIn(), comparator=cst.Name("bar")
                        ),
                    ),
                    rpar=(cst.RightParen(),),
                ),
                "(foo not in bar)",
            ),
            (
                cst.Comparison(
                    left=cst.Name(
                        "a", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                    ),
                    comparisons=(
                        cst.ComparisonTarget(
                            operator=cst.Is(
                                whitespace_before=cst.SimpleWhitespace(""),
                                whitespace_after=cst.SimpleWhitespace(""),
                            ),
                            comparator=cst.Name(
                                "b", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                            ),
                        ),
                        cst.ComparisonTarget(
                            operator=cst.Is(
                                whitespace_before=cst.SimpleWhitespace(""),
                                whitespace_after=cst.SimpleWhitespace(""),
                            ),
                            comparator=cst.Name(
                                "c", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                            ),
                        ),
                    ),
                ),
                "(a)is(b)is(c)",
            ),
            # Valid expressions that look like they shouldn't parse
            (
                cst.Comparison(
                    left=cst.Integer("5"),
                    comparisons=(
                        cst.ComparisonTarget(
                            operator=cst.NotIn(
                                whitespace_before=cst.SimpleWhitespace("")
                            ),
                            comparator=cst.Name("bar"),
                        ),
                    ),
                ),
                "5not in bar",
            ),
            # Validate that spacing works properly
            (
                cst.Comparison(
                    lpar=(cst.LeftParen(whitespace_after=cst.SimpleWhitespace(" ")),),
                    left=cst.Name("foo"),
                    comparisons=(
                        cst.ComparisonTarget(
                            operator=cst.NotIn(
                                whitespace_before=cst.SimpleWhitespace("  "),
                                whitespace_between=cst.SimpleWhitespace("  "),
                                whitespace_after=cst.SimpleWhitespace("  "),
                            ),
                            comparator=cst.Name("bar"),
                        ),
                    ),
                    rpar=(cst.RightParen(whitespace_before=cst.SimpleWhitespace(" ")),),
                ),
                "( foo  not  in  bar )",
            ),
            # Do some complex nodes
            (
                cst.Comparison(
                    left=cst.Name("baz"),
                    comparisons=(
                        cst.ComparisonTarget(
                            operator=cst.Equal(),
                            comparator=cst.Comparison(
                                lpar=(cst.LeftParen(),),
                                left=cst.Name("foo"),
                                comparisons=(
                                    cst.ComparisonTarget(
                                        operator=cst.NotIn(), comparator=cst.Name("bar")
                                    ),
                                ),
                                rpar=(cst.RightParen(),),
                            ),
                        ),
                    ),
                ),
                "baz == (foo not in bar)",
                CodeRange((1, 0), (1, 23)),
            ),
            (
                cst.Comparison(
                    left=cst.Name("a"),
                    comparisons=(
                        cst.ComparisonTarget(
                            operator=cst.GreaterThan(), comparator=cst.Name("b")
                        ),
                        cst.ComparisonTarget(
                            operator=cst.GreaterThan(), comparator=cst.Name("c")
                        ),
                    ),
                ),
                "a > b > c",
                CodeRange((1, 0), (1, 9)),
            ),
            # Is safe to use with word operators if it's leading/trailing children are
            (
                cst.IfExp(
                    body=cst.Comparison(
                        left=cst.Name("a"),
                        comparisons=(
                            cst.ComparisonTarget(
                                operator=cst.GreaterThan(),
                                comparator=cst.Name(
                                    "b",
                                    lpar=(cst.LeftParen(),),
                                    rpar=(cst.RightParen(),),
                                ),
                            ),
                        ),
                    ),
                    test=cst.Comparison(
                        left=cst.Name(
                            "c", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                        ),
                        comparisons=(
                            cst.ComparisonTarget(
                                operator=cst.GreaterThan(), comparator=cst.Name("d")
                            ),
                        ),
                    ),
                    orelse=cst.Name("e"),
                    whitespace_before_if=cst.SimpleWhitespace(""),
                    whitespace_after_if=cst.SimpleWhitespace(""),
                ),
                "a > (b)if(c) > d else e",
            ),
            # is safe to use with word operators if entirely surrounded in parenthesis
            (
                cst.IfExp(
                    body=cst.Name("a"),
                    test=cst.Comparison(
                        left=cst.Name("b"),
                        comparisons=(
                            cst.ComparisonTarget(
                                operator=cst.GreaterThan(), comparator=cst.Name("c")
                            ),
                        ),
                        lpar=(cst.LeftParen(),),
                        rpar=(cst.RightParen(),),
                    ),
                    orelse=cst.Name("d"),
                    whitespace_after_if=cst.SimpleWhitespace(""),
                    whitespace_before_else=cst.SimpleWhitespace(""),
                ),
                "a if(b > c)else d",
            ),
        )
    )
    def test_valid(
        self, node: cst.CSTNode, code: str, position: Optional[CodeRange] = None
    ) -> None:
        self.validate_node(node, code, parse_expression, expected_position=position)

    @data_provider(
        (
            (
                lambda: cst.Comparison(
                    cst.Name("foo"),
                    (cst.ComparisonTarget(cst.LessThan(), cst.Integer("5")),),
                    lpar=(cst.LeftParen(),),
                ),
                "left paren without right paren",
            ),
            (
                lambda: cst.Comparison(
                    cst.Name("foo"),
                    (cst.ComparisonTarget(cst.LessThan(), cst.Integer("5")),),
                    rpar=(cst.RightParen(),),
                ),
                "right paren without left paren",
            ),
            (
                lambda: cst.Comparison(cst.Name("foo"), ()),
                "at least one ComparisonTarget",
            ),
            (
                lambda: cst.Comparison(
                    left=cst.Name("foo"),
                    comparisons=(
                        cst.ComparisonTarget(
                            operator=cst.NotIn(
                                whitespace_before=cst.SimpleWhitespace("")
                            ),
                            comparator=cst.Name("bar"),
                        ),
                    ),
                ),
                "at least one space around comparison operator",
            ),
            (
                lambda: cst.Comparison(
                    left=cst.Name("foo"),
                    comparisons=(
                        cst.ComparisonTarget(
                            operator=cst.NotIn(
                                whitespace_after=cst.SimpleWhitespace("")
                            ),
                            comparator=cst.Name("bar"),
                        ),
                    ),
                ),
                "at least one space around comparison operator",
            ),
            # multi-target comparisons
            (
                lambda: cst.Comparison(
                    left=cst.Name("a"),
                    comparisons=(
                        cst.ComparisonTarget(
                            operator=cst.Is(), comparator=cst.Name("b")
                        ),
                        cst.ComparisonTarget(
                            operator=cst.Is(whitespace_before=cst.SimpleWhitespace("")),
                            comparator=cst.Name("c"),
                        ),
                    ),
                ),
                "at least one space around comparison operator",
            ),
            (
                lambda: cst.Comparison(
                    left=cst.Name("a"),
                    comparisons=(
                        cst.ComparisonTarget(
                            operator=cst.Is(), comparator=cst.Name("b")
                        ),
                        cst.ComparisonTarget(
                            operator=cst.Is(whitespace_after=cst.SimpleWhitespace("")),
                            comparator=cst.Name("c"),
                        ),
                    ),
                ),
                "at least one space around comparison operator",
            ),
            # whitespace around the comparision itself
            # a ifb > c else d
            (
                lambda: cst.IfExp(
                    body=cst.Name("a"),
                    test=cst.Comparison(
                        left=cst.Name("b"),
                        comparisons=(
                            cst.ComparisonTarget(
                                operator=cst.GreaterThan(), comparator=cst.Name("c")
                            ),
                        ),
                    ),
                    orelse=cst.Name("d"),
                    whitespace_after_if=cst.SimpleWhitespace(""),
                ),
                "Must have at least one space after 'if' keyword.",
            ),
            # a if b > celse d
            (
                lambda: cst.IfExp(
                    body=cst.Name("a"),
                    test=cst.Comparison(
                        left=cst.Name("b"),
                        comparisons=(
                            cst.ComparisonTarget(
                                operator=cst.GreaterThan(), comparator=cst.Name("c")
                            ),
                        ),
                    ),
                    orelse=cst.Name("d"),
                    whitespace_before_else=cst.SimpleWhitespace(""),
                ),
                "Must have at least one space before 'else' keyword.",
            ),
        )
    )
    def test_invalid(
        self, get_node: Callable[[], cst.CSTNode], expected_re: str
    ) -> None:
        self.assert_invalid(get_node, expected_re)
