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


class SubscriptTest(CSTNodeTest):
    @data_provider(
        (
            # Simple subscript expression
            (
                cst.Subscript(
                    cst.Name("foo"),
                    (cst.SubscriptElement(cst.Index(cst.Integer("5"))),),
                ),
                "foo[5]",
                True,
            ),
            # Test creation of subscript with slice/extslice.
            (
                cst.Subscript(
                    cst.Name("foo"),
                    (
                        cst.SubscriptElement(
                            cst.Slice(
                                lower=cst.Integer("1"),
                                upper=cst.Integer("2"),
                                step=cst.Integer("3"),
                            )
                        ),
                    ),
                ),
                "foo[1:2:3]",
                False,
            ),
            (
                cst.Subscript(
                    cst.Name("foo"),
                    (
                        cst.SubscriptElement(
                            cst.Slice(
                                lower=cst.Integer("1"),
                                upper=cst.Integer("2"),
                                step=cst.Integer("3"),
                            )
                        ),
                        cst.SubscriptElement(cst.Index(cst.Integer("5"))),
                    ),
                ),
                "foo[1:2:3, 5]",
                False,
                CodeRange((1, 0), (1, 13)),
            ),
            # Test parsing of subscript with slice/extslice.
            (
                cst.Subscript(
                    cst.Name("foo"),
                    (
                        cst.SubscriptElement(
                            cst.Slice(
                                lower=cst.Integer("1"),
                                first_colon=cst.Colon(),
                                upper=cst.Integer("2"),
                                second_colon=cst.Colon(),
                                step=cst.Integer("3"),
                            )
                        ),
                    ),
                ),
                "foo[1:2:3]",
                True,
            ),
            (
                cst.Subscript(
                    cst.Name("foo"),
                    (
                        cst.SubscriptElement(
                            cst.Slice(
                                lower=cst.Integer("1"),
                                first_colon=cst.Colon(),
                                upper=cst.Integer("2"),
                                second_colon=cst.Colon(),
                                step=cst.Integer("3"),
                            ),
                            comma=cst.Comma(),
                        ),
                        cst.SubscriptElement(cst.Index(cst.Integer("5"))),
                    ),
                ),
                "foo[1:2:3,5]",
                True,
            ),
            # Some more wild slice creations
            (
                cst.Subscript(
                    cst.Name("foo"),
                    (
                        cst.SubscriptElement(
                            cst.Slice(lower=cst.Integer("1"), upper=cst.Integer("2"))
                        ),
                    ),
                ),
                "foo[1:2]",
                True,
            ),
            (
                cst.Subscript(
                    cst.Name("foo"),
                    (
                        cst.SubscriptElement(
                            cst.Slice(lower=cst.Integer("1"), upper=None)
                        ),
                    ),
                ),
                "foo[1:]",
                True,
            ),
            (
                cst.Subscript(
                    cst.Name("foo"),
                    (
                        cst.SubscriptElement(
                            cst.Slice(lower=None, upper=cst.Integer("2"))
                        ),
                    ),
                ),
                "foo[:2]",
                True,
            ),
            (
                cst.Subscript(
                    cst.Name("foo"),
                    (
                        cst.SubscriptElement(
                            cst.Slice(
                                lower=cst.Integer("1"),
                                upper=None,
                                step=cst.Integer("3"),
                            )
                        ),
                    ),
                ),
                "foo[1::3]",
                False,
            ),
            (
                cst.Subscript(
                    cst.Name("foo"),
                    (
                        cst.SubscriptElement(
                            cst.Slice(lower=None, upper=None, step=cst.Integer("3"))
                        ),
                    ),
                ),
                "foo[::3]",
                False,
                CodeRange((1, 0), (1, 8)),
            ),
            # Some more wild slice parsings
            (
                cst.Subscript(
                    cst.Name("foo"),
                    (
                        cst.SubscriptElement(
                            cst.Slice(lower=cst.Integer("1"), upper=cst.Integer("2"))
                        ),
                    ),
                ),
                "foo[1:2]",
                True,
            ),
            (
                cst.Subscript(
                    cst.Name("foo"),
                    (
                        cst.SubscriptElement(
                            cst.Slice(lower=cst.Integer("1"), upper=None)
                        ),
                    ),
                ),
                "foo[1:]",
                True,
            ),
            (
                cst.Subscript(
                    cst.Name("foo"),
                    (
                        cst.SubscriptElement(
                            cst.Slice(lower=None, upper=cst.Integer("2"))
                        ),
                    ),
                ),
                "foo[:2]",
                True,
            ),
            (
                cst.Subscript(
                    cst.Name("foo"),
                    (
                        cst.SubscriptElement(
                            cst.Slice(
                                lower=cst.Integer("1"),
                                upper=None,
                                second_colon=cst.Colon(),
                                step=cst.Integer("3"),
                            )
                        ),
                    ),
                ),
                "foo[1::3]",
                True,
            ),
            (
                cst.Subscript(
                    cst.Name("foo"),
                    (
                        cst.SubscriptElement(
                            cst.Slice(
                                lower=None,
                                upper=None,
                                second_colon=cst.Colon(),
                                step=cst.Integer("3"),
                            )
                        ),
                    ),
                ),
                "foo[::3]",
                True,
            ),
            # Valid list clone operations rendering
            (
                cst.Subscript(
                    cst.Name("foo"),
                    (cst.SubscriptElement(cst.Slice(lower=None, upper=None)),),
                ),
                "foo[:]",
                True,
            ),
            (
                cst.Subscript(
                    cst.Name("foo"),
                    (
                        cst.SubscriptElement(
                            cst.Slice(
                                lower=None,
                                upper=None,
                                second_colon=cst.Colon(),
                                step=None,
                            )
                        ),
                    ),
                ),
                "foo[::]",
                True,
            ),
            # Valid list clone operations parsing
            (
                cst.Subscript(
                    cst.Name("foo"),
                    (cst.SubscriptElement(cst.Slice(lower=None, upper=None)),),
                ),
                "foo[:]",
                True,
            ),
            (
                cst.Subscript(
                    cst.Name("foo"),
                    (
                        cst.SubscriptElement(
                            cst.Slice(
                                lower=None,
                                upper=None,
                                second_colon=cst.Colon(),
                                step=None,
                            )
                        ),
                    ),
                ),
                "foo[::]",
                True,
            ),
            # In parenthesis
            (
                cst.Subscript(
                    lpar=(cst.LeftParen(),),
                    value=cst.Name("foo"),
                    slice=(cst.SubscriptElement(cst.Index(cst.Integer("5"))),),
                    rpar=(cst.RightParen(),),
                ),
                "(foo[5])",
                True,
            ),
            # Verify spacing
            (
                cst.Subscript(
                    lpar=(cst.LeftParen(whitespace_after=cst.SimpleWhitespace(" ")),),
                    value=cst.Name("foo"),
                    lbracket=cst.LeftSquareBracket(
                        whitespace_after=cst.SimpleWhitespace(" ")
                    ),
                    slice=(cst.SubscriptElement(cst.Index(cst.Integer("5"))),),
                    rbracket=cst.RightSquareBracket(
                        whitespace_before=cst.SimpleWhitespace(" ")
                    ),
                    rpar=(cst.RightParen(whitespace_before=cst.SimpleWhitespace(" ")),),
                    whitespace_after_value=cst.SimpleWhitespace(" "),
                ),
                "( foo [ 5 ] )",
                True,
            ),
            (
                cst.Subscript(
                    lpar=(cst.LeftParen(whitespace_after=cst.SimpleWhitespace(" ")),),
                    value=cst.Name("foo"),
                    lbracket=cst.LeftSquareBracket(
                        whitespace_after=cst.SimpleWhitespace(" ")
                    ),
                    slice=(
                        cst.SubscriptElement(
                            cst.Slice(
                                lower=cst.Integer("1"),
                                first_colon=cst.Colon(
                                    whitespace_before=cst.SimpleWhitespace(" "),
                                    whitespace_after=cst.SimpleWhitespace(" "),
                                ),
                                upper=cst.Integer("2"),
                                second_colon=cst.Colon(
                                    whitespace_before=cst.SimpleWhitespace(" "),
                                    whitespace_after=cst.SimpleWhitespace(" "),
                                ),
                                step=cst.Integer("3"),
                            )
                        ),
                    ),
                    rbracket=cst.RightSquareBracket(
                        whitespace_before=cst.SimpleWhitespace(" ")
                    ),
                    rpar=(cst.RightParen(whitespace_before=cst.SimpleWhitespace(" ")),),
                    whitespace_after_value=cst.SimpleWhitespace(" "),
                ),
                "( foo [ 1 : 2 : 3 ] )",
                True,
            ),
            (
                cst.Subscript(
                    lpar=(cst.LeftParen(whitespace_after=cst.SimpleWhitespace(" ")),),
                    value=cst.Name("foo"),
                    lbracket=cst.LeftSquareBracket(
                        whitespace_after=cst.SimpleWhitespace(" ")
                    ),
                    slice=(
                        cst.SubscriptElement(
                            slice=cst.Slice(
                                lower=cst.Integer("1"),
                                first_colon=cst.Colon(
                                    whitespace_before=cst.SimpleWhitespace(" "),
                                    whitespace_after=cst.SimpleWhitespace(" "),
                                ),
                                upper=cst.Integer("2"),
                                second_colon=cst.Colon(
                                    whitespace_before=cst.SimpleWhitespace(" "),
                                    whitespace_after=cst.SimpleWhitespace(" "),
                                ),
                                step=cst.Integer("3"),
                            ),
                            comma=cst.Comma(
                                whitespace_before=cst.SimpleWhitespace(" "),
                                whitespace_after=cst.SimpleWhitespace("  "),
                            ),
                        ),
                        cst.SubscriptElement(slice=cst.Index(cst.Integer("5"))),
                    ),
                    rbracket=cst.RightSquareBracket(
                        whitespace_before=cst.SimpleWhitespace(" ")
                    ),
                    rpar=(cst.RightParen(whitespace_before=cst.SimpleWhitespace(" ")),),
                    whitespace_after_value=cst.SimpleWhitespace(" "),
                ),
                "( foo [ 1 : 2 : 3 ,  5 ] )",
                True,
                CodeRange((1, 2), (1, 24)),
            ),
            # Test Index, Slice, SubscriptElement
            (cst.Index(cst.Integer("5")), "5", False, CodeRange((1, 0), (1, 1))),
            (
                cst.Slice(lower=None, upper=None, second_colon=cst.Colon(), step=None),
                "::",
                False,
                CodeRange((1, 0), (1, 2)),
            ),
            (
                cst.SubscriptElement(
                    slice=cst.Slice(
                        lower=cst.Integer("1"),
                        first_colon=cst.Colon(
                            whitespace_before=cst.SimpleWhitespace(" "),
                            whitespace_after=cst.SimpleWhitespace(" "),
                        ),
                        upper=cst.Integer("2"),
                        second_colon=cst.Colon(
                            whitespace_before=cst.SimpleWhitespace(" "),
                            whitespace_after=cst.SimpleWhitespace(" "),
                        ),
                        step=cst.Integer("3"),
                    ),
                    comma=cst.Comma(
                        whitespace_before=cst.SimpleWhitespace(" "),
                        whitespace_after=cst.SimpleWhitespace("  "),
                    ),
                ),
                "1 : 2 : 3 ,  ",
                False,
                CodeRange((1, 0), (1, 9)),
            ),
        )
    )
    def test_valid(
        self,
        node: cst.CSTNode,
        code: str,
        check_parsing: bool,
        position: Optional[CodeRange] = None,
    ) -> None:
        if check_parsing:
            self.validate_node(node, code, parse_expression, expected_position=position)
        else:
            self.validate_node(node, code, expected_position=position)

    @data_provider(
        (
            (
                lambda: cst.Subscript(
                    cst.Name("foo"),
                    (cst.SubscriptElement(cst.Index(cst.Integer("5"))),),
                    lpar=(cst.LeftParen(),),
                ),
                "left paren without right paren",
            ),
            (
                lambda: cst.Subscript(
                    cst.Name("foo"),
                    (cst.SubscriptElement(cst.Index(cst.Integer("5"))),),
                    rpar=(cst.RightParen(),),
                ),
                "right paren without left paren",
            ),
            (lambda: cst.Subscript(cst.Name("foo"), ()), "empty SubscriptElement"),
        )
    )
    def test_invalid(
        self, get_node: Callable[[], cst.CSTNode], expected_re: str
    ) -> None:
        self.assert_invalid(get_node, expected_re)
