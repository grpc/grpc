# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any

import libcst as cst
from libcst import parse_expression
from libcst._nodes.tests.base import CSTNodeTest
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider


class CallTest(CSTNodeTest):
    @data_provider(
        (
            # Simple call
            {
                "node": cst.Call(cst.Name("foo")),
                "code": "foo()",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Call(
                    cst.Name("foo"), whitespace_before_args=cst.SimpleWhitespace(" ")
                ),
                "code": "foo( )",
                "parser": parse_expression,
                "expected_position": None,
            },
            # Call with attribute dereference
            {
                "node": cst.Call(cst.Attribute(cst.Name("foo"), cst.Name("bar"))),
                "code": "foo.bar()",
                "parser": parse_expression,
                "expected_position": None,
            },
            # Positional arguments render test
            {
                "node": cst.Call(cst.Name("foo"), (cst.Arg(cst.Integer("1")),)),
                "code": "foo(1)",
                "parser": None,
                "expected_position": None,
            },
            {
                "node": cst.Call(
                    cst.Name("foo"),
                    (
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                "code": "foo(1, 2, 3)",
                "parser": None,
                "expected_position": None,
            },
            # Positional arguments parse test
            {
                "node": cst.Call(cst.Name("foo"), (cst.Arg(value=cst.Integer("1")),)),
                "code": "foo(1)",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Call(
                    cst.Name("foo"),
                    (
                        cst.Arg(
                            value=cst.Integer("1"),
                            whitespace_after_arg=cst.SimpleWhitespace(" "),
                        ),
                    ),
                    whitespace_after_func=cst.SimpleWhitespace(" "),
                    whitespace_before_args=cst.SimpleWhitespace(" "),
                ),
                "code": "foo ( 1 )",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Call(
                    cst.Name("foo"),
                    (
                        cst.Arg(
                            value=cst.Integer("1"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                    ),
                    whitespace_after_func=cst.SimpleWhitespace(" "),
                    whitespace_before_args=cst.SimpleWhitespace(" "),
                ),
                "code": "foo ( 1, )",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Call(
                    cst.Name("foo"),
                    (
                        cst.Arg(
                            value=cst.Integer("1"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            value=cst.Integer("2"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(value=cst.Integer("3")),
                    ),
                ),
                "code": "foo(1, 2, 3)",
                "parser": parse_expression,
                "expected_position": None,
            },
            # Keyword arguments render test
            {
                "node": cst.Call(
                    cst.Name("foo"),
                    (cst.Arg(keyword=cst.Name("one"), value=cst.Integer("1")),),
                ),
                "code": "foo(one = 1)",
                "parser": None,
                "expected_position": None,
            },
            {
                "node": cst.Call(
                    cst.Name("foo"),
                    (
                        cst.Arg(keyword=cst.Name("one"), value=cst.Integer("1")),
                        cst.Arg(keyword=cst.Name("two"), value=cst.Integer("2")),
                        cst.Arg(keyword=cst.Name("three"), value=cst.Integer("3")),
                    ),
                ),
                "code": "foo(one = 1, two = 2, three = 3)",
                "parser": None,
                "expected_position": None,
            },
            # Keyword arguments parser test
            {
                "node": cst.Call(
                    cst.Name("foo"),
                    (
                        cst.Arg(
                            keyword=cst.Name("one"),
                            equal=cst.AssignEqual(),
                            value=cst.Integer("1"),
                        ),
                    ),
                ),
                "code": "foo(one = 1)",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Call(
                    cst.Name("foo"),
                    (
                        cst.Arg(
                            keyword=cst.Name("one"),
                            equal=cst.AssignEqual(),
                            value=cst.Integer("1"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            keyword=cst.Name("two"),
                            equal=cst.AssignEqual(),
                            value=cst.Integer("2"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            keyword=cst.Name("three"),
                            equal=cst.AssignEqual(),
                            value=cst.Integer("3"),
                        ),
                    ),
                ),
                "code": "foo(one = 1, two = 2, three = 3)",
                "parser": parse_expression,
                "expected_position": None,
            },
            # Iterator expansion render test
            {
                "node": cst.Call(
                    cst.Name("foo"), (cst.Arg(star="*", value=cst.Name("one")),)
                ),
                "code": "foo(*one)",
                "parser": None,
                "expected_position": None,
            },
            {
                "node": cst.Call(
                    cst.Name("foo"),
                    (
                        cst.Arg(star="*", value=cst.Name("one")),
                        cst.Arg(star="*", value=cst.Name("two")),
                        cst.Arg(star="*", value=cst.Name("three")),
                    ),
                ),
                "code": "foo(*one, *two, *three)",
                "parser": None,
                "expected_position": None,
            },
            # Iterator expansion parser test
            {
                "node": cst.Call(
                    cst.Name("foo"), (cst.Arg(star="*", value=cst.Name("one")),)
                ),
                "code": "foo(*one)",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Call(
                    cst.Name("foo"),
                    (
                        cst.Arg(
                            star="*",
                            value=cst.Name("one"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            star="*",
                            value=cst.Name("two"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(star="*", value=cst.Name("three")),
                    ),
                ),
                "code": "foo(*one, *two, *three)",
                "parser": parse_expression,
                "expected_position": None,
            },
            # Dictionary expansion render test
            {
                "node": cst.Call(
                    cst.Name("foo"), (cst.Arg(star="**", value=cst.Name("one")),)
                ),
                "code": "foo(**one)",
                "parser": None,
                "expected_position": None,
            },
            {
                "node": cst.Call(
                    cst.Name("foo"),
                    (
                        cst.Arg(star="**", value=cst.Name("one")),
                        cst.Arg(star="**", value=cst.Name("two")),
                        cst.Arg(star="**", value=cst.Name("three")),
                    ),
                ),
                "code": "foo(**one, **two, **three)",
                "parser": None,
                "expected_position": None,
            },
            # Dictionary expansion parser test
            {
                "node": cst.Call(
                    cst.Name("foo"), (cst.Arg(star="**", value=cst.Name("one")),)
                ),
                "code": "foo(**one)",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Call(
                    cst.Name("foo"),
                    (
                        cst.Arg(
                            star="**",
                            value=cst.Name("one"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            star="**",
                            value=cst.Name("two"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(star="**", value=cst.Name("three")),
                    ),
                ),
                "code": "foo(**one, **two, **three)",
                "parser": parse_expression,
                "expected_position": None,
            },
            # Complicated mingling rules render test
            {
                "node": cst.Call(
                    cst.Name("foo"),
                    (
                        cst.Arg(value=cst.Name("pos1")),
                        cst.Arg(star="*", value=cst.Name("list1")),
                        cst.Arg(value=cst.Name("pos2")),
                        cst.Arg(value=cst.Name("pos3")),
                        cst.Arg(star="*", value=cst.Name("list2")),
                        cst.Arg(value=cst.Name("pos4")),
                        cst.Arg(star="*", value=cst.Name("list3")),
                        cst.Arg(keyword=cst.Name("kw1"), value=cst.Integer("1")),
                        cst.Arg(star="*", value=cst.Name("list4")),
                        cst.Arg(keyword=cst.Name("kw2"), value=cst.Integer("2")),
                        cst.Arg(star="*", value=cst.Name("list5")),
                        cst.Arg(keyword=cst.Name("kw3"), value=cst.Integer("3")),
                        cst.Arg(star="**", value=cst.Name("dict1")),
                        cst.Arg(keyword=cst.Name("kw4"), value=cst.Integer("4")),
                        cst.Arg(star="**", value=cst.Name("dict2")),
                    ),
                ),
                "code": "foo(pos1, *list1, pos2, pos3, *list2, pos4, *list3, kw1 = 1, *list4, kw2 = 2, *list5, kw3 = 3, **dict1, kw4 = 4, **dict2)",
                "parser": None,
                "expected_position": None,
            },
            # Complicated mingling rules parser test
            {
                "node": cst.Call(
                    cst.Name("foo"),
                    (
                        cst.Arg(
                            value=cst.Name("pos1"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            star="*",
                            value=cst.Name("list1"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            value=cst.Name("pos2"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            value=cst.Name("pos3"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            star="*",
                            value=cst.Name("list2"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            value=cst.Name("pos4"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            star="*",
                            value=cst.Name("list3"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            keyword=cst.Name("kw1"),
                            equal=cst.AssignEqual(),
                            value=cst.Integer("1"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            star="*",
                            value=cst.Name("list4"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            keyword=cst.Name("kw2"),
                            equal=cst.AssignEqual(),
                            value=cst.Integer("2"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            star="*",
                            value=cst.Name("list5"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            keyword=cst.Name("kw3"),
                            equal=cst.AssignEqual(),
                            value=cst.Integer("3"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            star="**",
                            value=cst.Name("dict1"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            keyword=cst.Name("kw4"),
                            equal=cst.AssignEqual(),
                            value=cst.Integer("4"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(star="**", value=cst.Name("dict2")),
                    ),
                ),
                "code": "foo(pos1, *list1, pos2, pos3, *list2, pos4, *list3, kw1 = 1, *list4, kw2 = 2, *list5, kw3 = 3, **dict1, kw4 = 4, **dict2)",
                "parser": parse_expression,
                "expected_position": None,
            },
            # Test whitespace
            {
                "node": cst.Call(
                    lpar=(cst.LeftParen(whitespace_after=cst.SimpleWhitespace(" ")),),
                    func=cst.Name("foo"),
                    whitespace_after_func=cst.SimpleWhitespace(" "),
                    whitespace_before_args=cst.SimpleWhitespace(" "),
                    args=(
                        cst.Arg(
                            keyword=None,
                            value=cst.Name("pos1"),
                            comma=cst.Comma(
                                whitespace_before=cst.SimpleWhitespace(" "),
                                whitespace_after=cst.SimpleWhitespace("  "),
                            ),
                        ),
                        cst.Arg(
                            star="*",
                            whitespace_after_star=cst.SimpleWhitespace("  "),
                            keyword=None,
                            value=cst.Name("list1"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            keyword=cst.Name("kw1"),
                            equal=cst.AssignEqual(
                                whitespace_before=cst.SimpleWhitespace(""),
                                whitespace_after=cst.SimpleWhitespace(""),
                            ),
                            value=cst.Integer("1"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            star="**",
                            keyword=None,
                            whitespace_after_star=cst.SimpleWhitespace(" "),
                            value=cst.Name("dict1"),
                            whitespace_after_arg=cst.SimpleWhitespace(" "),
                        ),
                    ),
                    rpar=(cst.RightParen(whitespace_before=cst.SimpleWhitespace(" ")),),
                ),
                "code": "( foo ( pos1 ,  *  list1, kw1=1, ** dict1 ) )",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 2), (1, 43)),
            },
            # Test args
            {
                "node": cst.Arg(
                    star="*",
                    whitespace_after_star=cst.SimpleWhitespace("  "),
                    keyword=None,
                    value=cst.Name("list1"),
                    comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                ),
                "code": "*  list1, ",
                "parser": None,
                "expected_position": CodeRange((1, 0), (1, 8)),
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            # Basic expression parenthesizing tests.
            {
                "get_node": lambda: cst.Call(
                    func=cst.Name("foo"), lpar=(cst.LeftParen(),)
                ),
                "expected_re": "left paren without right paren",
            },
            {
                "get_node": lambda: cst.Call(
                    func=cst.Name("foo"), rpar=(cst.RightParen(),)
                ),
                "expected_re": "right paren without left paren",
            },
            # Test that we handle keyword stuff correctly.
            {
                "get_node": lambda: cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(
                            equal=cst.AssignEqual(), value=cst.SimpleString("'baz'")
                        ),
                    ),
                ),
                "expected_re": "Must have a keyword when specifying an AssignEqual",
            },
            # Test that we separate *, ** and keyword args correctly
            {
                "get_node": lambda: cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(
                            star="*",
                            keyword=cst.Name("bar"),
                            value=cst.SimpleString("'baz'"),
                        ),
                    ),
                ),
                "expected_re": "Cannot specify a star and a keyword together",
            },
            # Test for expected star inputs only
            {
                "get_node": lambda: cst.Call(
                    func=cst.Name("foo"),
                    # pyre-ignore: Ignore type on 'star' since we're testing behavior
                    # when somebody isn't using a type checker.
                    args=(cst.Arg(star="***", value=cst.SimpleString("'baz'")),),
                ),
                "expected_re": r"Must specify either '', '\*' or '\*\*' for star",
            },
            # Test ordering exceptions
            {
                "get_node": lambda: cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(star="**", value=cst.Name("bar")),
                        cst.Arg(star="*", value=cst.Name("baz")),
                    ),
                ),
                "expected_re": "Cannot have iterable argument unpacking after keyword argument unpacking",
            },
            {
                "get_node": lambda: cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(star="**", value=cst.Name("bar")),
                        cst.Arg(value=cst.Name("baz")),
                    ),
                ),
                "expected_re": "Cannot have positional argument after keyword argument unpacking",
            },
            {
                "get_node": lambda: cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(
                            keyword=cst.Name("arg"), value=cst.SimpleString("'baz'")
                        ),
                        cst.Arg(value=cst.SimpleString("'bar'")),
                    ),
                ),
                "expected_re": "Cannot have positional argument after keyword argument",
            },
        )
    )
    def test_invalid(self, **kwargs: Any) -> None:
        self.assert_invalid(**kwargs)
