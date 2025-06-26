# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


from typing import Any

import libcst as cst
from libcst import parse_expression
from libcst._nodes.tests.base import CSTNodeTest, parse_expression_as
from libcst._parser.entrypoints import is_native
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider


def _parse_expression_force_38(code: str) -> cst.BaseExpression:
    return cst.parse_expression(
        code, config=cst.PartialParserConfig(python_version="3.8")
    )


class AtomTest(CSTNodeTest):
    @data_provider(
        (
            # Simple identifier
            {
                "node": cst.Name("test"),
                "code": "test",
                "parser": parse_expression,
                "expected_position": None,
            },
            # Parenthesized identifier
            {
                "node": cst.Name(
                    "test", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                ),
                "code": "(test)",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 1), (1, 5)),
            },
            # Decimal integers
            {
                "node": cst.Integer("12345"),
                "code": "12345",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Integer("0000"),
                "code": "0000",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Integer("1_234_567"),
                "code": "1_234_567",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Integer("0_000"),
                "code": "0_000",
                "parser": parse_expression,
                "expected_position": None,
            },
            # Binary integers
            {
                "node": cst.Integer("0b0000"),
                "code": "0b0000",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Integer("0B1011_0100"),
                "code": "0B1011_0100",
                "parser": parse_expression,
                "expected_position": None,
            },
            # Octal integers
            {
                "node": cst.Integer("0o12345"),
                "code": "0o12345",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Integer("0O12_345"),
                "code": "0O12_345",
                "parser": parse_expression,
                "expected_position": None,
            },
            # Hex numbers
            {
                "node": cst.Integer("0x123abc"),
                "code": "0x123abc",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Integer("0X12_3ABC"),
                "code": "0X12_3ABC",
                "parser": parse_expression,
                "expected_position": None,
            },
            # Parenthesized integers
            {
                "node": cst.Integer(
                    "123", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                ),
                "code": "(123)",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 1), (1, 4)),
            },
            # Non-exponent floats
            {
                "node": cst.Float("12345."),
                "code": "12345.",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Float("00.00"),
                "code": "00.00",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Float("12.21"),
                "code": "12.21",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Float(".321"),
                "code": ".321",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Float("1_234_567."),
                "code": "1_234_567.",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Float("0.000_000"),
                "code": "0.000_000",
                "parser": parse_expression,
                "expected_position": None,
            },
            # Exponent floats
            {
                "node": cst.Float("12345.e10"),
                "code": "12345.e10",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Float("00.00e10"),
                "code": "00.00e10",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Float("12.21e10"),
                "code": "12.21e10",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Float(".321e10"),
                "code": ".321e10",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Float("1_234_567.e10"),
                "code": "1_234_567.e10",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Float("0.000_000e10"),
                "code": "0.000_000e10",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Float("1e+10"),
                "code": "1e+10",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Float("1e-10"),
                "code": "1e-10",
                "parser": parse_expression,
                "expected_position": None,
            },
            # Parenthesized floats
            {
                "node": cst.Float(
                    "123.4", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                ),
                "code": "(123.4)",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 1), (1, 6)),
            },
            # Imaginary numbers
            {
                "node": cst.Imaginary("12345j"),
                "code": "12345j",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Imaginary("1_234_567J"),
                "code": "1_234_567J",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Imaginary("12345.e10j"),
                "code": "12345.e10j",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.Imaginary(".321J"),
                "code": ".321J",
                "parser": parse_expression,
                "expected_position": None,
            },
            # Parenthesized imaginary
            {
                "node": cst.Imaginary(
                    "123.4j", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                ),
                "code": "(123.4j)",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 1), (1, 7)),
            },
            # Simple elipses
            {
                "node": cst.Ellipsis(),
                "code": "...",
                "parser": parse_expression,
                "expected_position": None,
            },
            # Parenthesized elipses
            {
                "node": cst.Ellipsis(lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)),
                "code": "(...)",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 1), (1, 4)),
            },
            # Simple strings
            {
                "node": cst.SimpleString('""'),
                "code": '""',
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.SimpleString("''"),
                "code": "''",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.SimpleString('"test"'),
                "code": '"test"',
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.SimpleString('b"test"'),
                "code": 'b"test"',
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.SimpleString('r"test"'),
                "code": 'r"test"',
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.SimpleString('"""test"""'),
                "code": '"""test"""',
                "parser": parse_expression,
                "expected_position": None,
            },
            # Validate parens
            {
                "node": cst.SimpleString(
                    '"test"', lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                ),
                "code": '("test")',
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.SimpleString(
                    'rb"test"', lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                ),
                "code": '(rb"test")',
                "parser": parse_expression,
                "expected_position": CodeRange((1, 1), (1, 9)),
            },
            # Test that _safe_to_use_with_word_operator allows no space around quotes
            {
                "node": cst.Comparison(
                    cst.SimpleString('"a"'),
                    [
                        cst.ComparisonTarget(
                            cst.In(
                                whitespace_before=cst.SimpleWhitespace(""),
                                whitespace_after=cst.SimpleWhitespace(""),
                            ),
                            cst.SimpleString('"abc"'),
                        )
                    ],
                ),
                "code": '"a"in"abc"',
                "parser": parse_expression,
            },
            {
                "node": cst.Comparison(
                    cst.SimpleString('"a"'),
                    [
                        cst.ComparisonTarget(
                            cst.In(
                                whitespace_before=cst.SimpleWhitespace(""),
                                whitespace_after=cst.SimpleWhitespace(""),
                            ),
                            cst.ConcatenatedString(
                                cst.SimpleString('"a"'), cst.SimpleString('"bc"')
                            ),
                        )
                    ],
                ),
                "code": '"a"in"a""bc"',
                "parser": parse_expression,
            },
            # Parenthesis make no spaces around a prefix okay
            {
                "node": cst.Comparison(
                    cst.SimpleString('b"a"'),
                    [
                        cst.ComparisonTarget(
                            cst.In(
                                whitespace_before=cst.SimpleWhitespace(""),
                                whitespace_after=cst.SimpleWhitespace(""),
                            ),
                            cst.SimpleString(
                                'b"abc"',
                                lpar=[cst.LeftParen()],
                                rpar=[cst.RightParen()],
                            ),
                        )
                    ],
                ),
                "code": 'b"a"in(b"abc")',
                "parser": parse_expression,
            },
            {
                "node": cst.Comparison(
                    cst.SimpleString('b"a"'),
                    [
                        cst.ComparisonTarget(
                            cst.In(
                                whitespace_before=cst.SimpleWhitespace(""),
                                whitespace_after=cst.SimpleWhitespace(""),
                            ),
                            cst.ConcatenatedString(
                                cst.SimpleString('b"a"'),
                                cst.SimpleString('b"bc"'),
                                lpar=[cst.LeftParen()],
                                rpar=[cst.RightParen()],
                            ),
                        )
                    ],
                ),
                "code": 'b"a"in(b"a"b"bc")',
                "parser": parse_expression,
            },
            # Empty formatted strings
            {
                "node": cst.FormattedString(start='f"', parts=(), end='"'),
                "code": 'f""',
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.FormattedString(start="f'", parts=(), end="'"),
                "code": "f''",
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.FormattedString(start='f"""', parts=(), end='"""'),
                "code": 'f""""""',
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.FormattedString(start="f'''", parts=(), end="'''"),
                "code": "f''''''",
                "parser": parse_expression,
                "expected_position": None,
            },
            # Non-empty formatted strings
            {
                "node": cst.FormattedString(parts=(cst.FormattedStringText("foo"),)),
                "code": 'f"foo"',
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.FormattedString(
                    parts=(cst.FormattedStringExpression(cst.Name("foo")),)
                ),
                "code": 'f"{foo}"',
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.FormattedString(
                    parts=(
                        cst.FormattedStringText("foo "),
                        cst.FormattedStringExpression(cst.Name("bar")),
                        cst.FormattedStringText(" baz"),
                    )
                ),
                "code": 'f"foo {bar} baz"',
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.FormattedString(
                    parts=(
                        cst.FormattedStringText("foo "),
                        cst.FormattedStringExpression(cst.Call(cst.Name("bar"))),
                        cst.FormattedStringText(" baz"),
                    )
                ),
                "code": 'f"foo {bar()} baz"',
                "parser": parse_expression,
                "expected_position": None,
            },
            # Formatted strings with conversions and format specifiers
            {
                "node": cst.FormattedString(
                    parts=(
                        cst.FormattedStringExpression(cst.Name("foo"), conversion="s"),
                    )
                ),
                "code": 'f"{foo!s}"',
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.FormattedString(
                    parts=(
                        cst.FormattedStringExpression(cst.Name("foo"), format_spec=()),
                    )
                ),
                "code": 'f"{foo:}"',
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.FormattedString(
                    parts=(
                        cst.FormattedStringExpression(
                            cst.Name("today"),
                            format_spec=(cst.FormattedStringText("%B %d, %Y"),),
                        ),
                    )
                ),
                "code": 'f"{today:%B %d, %Y}"',
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.FormattedString(
                    parts=(
                        cst.FormattedStringExpression(
                            cst.Name("foo"),
                            format_spec=(
                                cst.FormattedStringExpression(cst.Name("bar")),
                            ),
                        ),
                    )
                ),
                "code": 'f"{foo:{bar}}"',
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.FormattedString(
                    parts=(
                        cst.FormattedStringExpression(
                            cst.Name("foo"),
                            format_spec=(
                                cst.FormattedStringExpression(cst.Name("bar")),
                                cst.FormattedStringText("."),
                                cst.FormattedStringExpression(cst.Name("baz")),
                            ),
                        ),
                    )
                ),
                "code": 'f"{foo:{bar}.{baz}}"',
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.FormattedString(
                    parts=(
                        cst.FormattedStringExpression(
                            cst.Name("foo"),
                            conversion="s",
                            format_spec=(
                                cst.FormattedStringExpression(cst.Name("bar")),
                            ),
                        ),
                    )
                ),
                "code": 'f"{foo!s:{bar}}"',
                "parser": parse_expression,
                "expected_position": None,
            },
            # Test equality expression added in 3.8.
            {
                "node": cst.FormattedString(
                    parts=(
                        cst.FormattedStringExpression(
                            cst.Name("foo"),
                            equal=cst.AssignEqual(
                                whitespace_before=cst.SimpleWhitespace(""),
                                whitespace_after=cst.SimpleWhitespace(""),
                            ),
                        ),
                    ),
                ),
                "code": 'f"{foo=}"',
                "parser": _parse_expression_force_38,
                "expected_position": None,
            },
            {
                "node": cst.FormattedString(
                    parts=(
                        cst.FormattedStringExpression(
                            cst.Name("foo"),
                            equal=cst.AssignEqual(
                                whitespace_before=cst.SimpleWhitespace(""),
                                whitespace_after=cst.SimpleWhitespace(""),
                            ),
                            conversion="s",
                        ),
                    ),
                ),
                "code": 'f"{foo=!s}"',
                "parser": _parse_expression_force_38,
                "expected_position": None,
            },
            {
                "node": cst.FormattedString(
                    parts=(
                        cst.FormattedStringExpression(
                            cst.Name("foo"),
                            equal=cst.AssignEqual(
                                whitespace_before=cst.SimpleWhitespace(""),
                                whitespace_after=cst.SimpleWhitespace(""),
                            ),
                            conversion="s",
                            format_spec=(
                                cst.FormattedStringExpression(cst.Name("bar")),
                            ),
                        ),
                    ),
                ),
                "code": 'f"{foo=!s:{bar}}"',
                "parser": _parse_expression_force_38,
                "expected_position": None,
            },
            # Test that equality support doesn't break existing support
            {
                "node": cst.FormattedString(
                    parts=(
                        cst.FormattedStringExpression(
                            cst.Comparison(
                                left=cst.Name(
                                    value="a",
                                ),
                                comparisons=[
                                    cst.ComparisonTarget(
                                        operator=cst.Equal(),
                                        comparator=cst.Name(
                                            value="b",
                                        ),
                                    ),
                                ],
                            ),
                        ),
                    ),
                ),
                "code": 'f"{a == b}"',
                "parser": _parse_expression_force_38,
                "expected_position": None,
            },
            {
                "node": cst.FormattedString(
                    parts=(
                        cst.FormattedStringExpression(
                            cst.Comparison(
                                left=cst.Name(
                                    value="a",
                                ),
                                comparisons=[
                                    cst.ComparisonTarget(
                                        operator=cst.NotEqual(),
                                        comparator=cst.Name(
                                            value="b",
                                        ),
                                    ),
                                ],
                            ),
                        ),
                    ),
                ),
                "code": 'f"{a != b}"',
                "parser": _parse_expression_force_38,
                "expected_position": None,
            },
            {
                "node": cst.FormattedString(
                    parts=(
                        cst.FormattedStringExpression(
                            cst.NamedExpr(
                                target=cst.Name(
                                    value="a",
                                ),
                                value=cst.Integer(
                                    value="5",
                                ),
                                lpar=(cst.LeftParen(),),
                                rpar=(cst.RightParen(),),
                            ),
                        ),
                    ),
                ),
                "code": 'f"{(a := 5)}"',
                "parser": _parse_expression_force_38,
                "expected_position": None,
            },
            {
                "node": cst.FormattedString(
                    parts=(
                        cst.FormattedStringExpression(
                            cst.Yield(
                                value=cst.Integer("1"),
                                whitespace_after_yield=cst.SimpleWhitespace(" "),
                            ),
                        ),
                    ),
                ),
                "code": 'f"{yield 1}"',
                "parser": _parse_expression_force_38,
                "expected_position": None,
            },
            {
                "node": cst.FormattedString(
                    parts=(
                        cst.FormattedStringText("\\N{X Y}"),
                        cst.FormattedStringExpression(
                            cst.Name(value="Z"),
                        ),
                    ),
                ),
                "code": 'f"\\N{X Y}{Z}"',
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.FormattedString(
                    parts=(
                        cst.FormattedStringText("\\"),
                        cst.FormattedStringExpression(
                            cst.Name(value="a"),
                        ),
                    ),
                    start='fr"',
                ),
                "code": 'fr"\\{a}"',
                "parser": parse_expression,
                "expected_position": None,
            },
            # Validate parens
            {
                "node": cst.FormattedString(
                    start='f"',
                    parts=(),
                    end='"',
                    lpar=(cst.LeftParen(),),
                    rpar=(cst.RightParen(),),
                ),
                "code": '(f"")',
                "parser": parse_expression,
                "expected_position": CodeRange((1, 1), (1, 4)),
            },
            # Generator expression (doesn't make sense, but legal syntax)
            {
                "node": cst.FormattedString(
                    start='f"',
                    parts=[
                        cst.FormattedStringExpression(
                            expression=cst.GeneratorExp(
                                elt=cst.Name(
                                    value="x",
                                ),
                                for_in=cst.CompFor(
                                    target=cst.Name(
                                        value="x",
                                    ),
                                    iter=cst.Name(
                                        value="y",
                                    ),
                                ),
                                lpar=[],
                                rpar=[],
                            ),
                        ),
                    ],
                    end='"',
                ),
                "code": 'f"{x for x in y}"',
                "parser": parse_expression,
                "expected_position": None,
            },
            # Unpacked tuple
            {
                "node": cst.FormattedString(
                    parts=[
                        cst.FormattedStringExpression(
                            expression=cst.Tuple(
                                elements=[
                                    cst.Element(
                                        value=cst.Name(
                                            value="a",
                                        ),
                                        comma=cst.Comma(
                                            whitespace_before=cst.SimpleWhitespace(
                                                value="",
                                            ),
                                            whitespace_after=cst.SimpleWhitespace(
                                                value=" ",
                                            ),
                                        ),
                                    ),
                                    cst.Element(
                                        value=cst.Name(
                                            value="b",
                                        ),
                                    ),
                                ],
                                lpar=[],
                                rpar=[],
                            ),
                        ),
                    ],
                    start="f'",
                    end="'",
                ),
                "code": "f'{a, b}'",
                "parser": parse_expression,
                "expected_position": None,
            },
            # Conditional expression
            {
                "node": cst.FormattedString(
                    parts=[
                        cst.FormattedStringExpression(
                            expression=cst.IfExp(
                                test=cst.Name(
                                    value="b",
                                ),
                                body=cst.Name(
                                    value="a",
                                ),
                                orelse=cst.Name(
                                    value="c",
                                ),
                            ),
                        ),
                    ],
                    start="f'",
                    end="'",
                ),
                "code": "f'{a if b else c}'",
                "parser": parse_expression,
                "expected_position": None,
            },
            # Concatenated strings
            {
                "node": cst.ConcatenatedString(
                    cst.SimpleString('"ab"'), cst.SimpleString('"c"')
                ),
                "code": '"ab""c"',
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.ConcatenatedString(
                    cst.SimpleString('"ab"'),
                    cst.ConcatenatedString(
                        cst.SimpleString('"c"'), cst.SimpleString('"d"')
                    ),
                ),
                "code": '"ab""c""d"',
                "parser": parse_expression,
                "expected_position": None,
            },
            # mixed SimpleString and FormattedString
            {
                "node": cst.ConcatenatedString(
                    cst.FormattedString([cst.FormattedStringText("ab")]),
                    cst.SimpleString('"c"'),
                ),
                "code": 'f"ab""c"',
                "parser": parse_expression,
                "expected_position": None,
            },
            {
                "node": cst.ConcatenatedString(
                    cst.SimpleString('"ab"'),
                    cst.FormattedString([cst.FormattedStringText("c")]),
                ),
                "code": '"ab"f"c"',
                "parser": parse_expression,
                "expected_position": None,
            },
            # Concatenated parenthesized strings
            {
                "node": cst.ConcatenatedString(
                    lpar=(cst.LeftParen(),),
                    left=cst.SimpleString('"ab"'),
                    right=cst.SimpleString('"c"'),
                    rpar=(cst.RightParen(),),
                ),
                "code": '("ab""c")',
                "parser": parse_expression,
                "expected_position": None,
            },
            # Validate spacing
            {
                "node": cst.ConcatenatedString(
                    lpar=(cst.LeftParen(whitespace_after=cst.SimpleWhitespace(" ")),),
                    left=cst.SimpleString('"ab"'),
                    whitespace_between=cst.SimpleWhitespace(" "),
                    right=cst.SimpleString('"c"'),
                    rpar=(cst.RightParen(whitespace_before=cst.SimpleWhitespace(" ")),),
                ),
                "code": '( "ab" "c" )',
                "parser": parse_expression,
                "expected_position": CodeRange((1, 2), (1, 10)),
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        # We don't have sentinel nodes for atoms, so we know that 100% of atoms
        # can be parsed identically to their creation.
        self.validate_node(**kwargs)

    @data_provider(
        (
            {
                "node": cst.FormattedStringExpression(
                    cst.Name("today"),
                    format_spec=(cst.FormattedStringText("%B %d, %Y"),),
                ),
                "code": "{today:%B %d, %Y}",
                "parser": None,
                "expected_position": CodeRange((1, 0), (1, 17)),
            },
        )
    )
    def test_valid_no_parse(self, **kwargs: Any) -> None:
        # Test some nodes that aren't valid source code by themselves
        self.validate_node(**kwargs)

    @data_provider(
        (
            # Expression wrapping parenthesis rules
            {
                "get_node": (lambda: cst.Name("foo", lpar=(cst.LeftParen(),))),
                "expected_re": "left paren without right paren",
            },
            {
                "get_node": lambda: cst.Name("foo", rpar=(cst.RightParen(),)),
                "expected_re": "right paren without left paren",
            },
            {
                "get_node": lambda: cst.Ellipsis(lpar=(cst.LeftParen(),)),
                "expected_re": "left paren without right paren",
            },
            {
                "get_node": lambda: cst.Ellipsis(rpar=(cst.RightParen(),)),
                "expected_re": "right paren without left paren",
            },
            {
                "get_node": lambda: cst.Integer("5", lpar=(cst.LeftParen(),)),
                "expected_re": "left paren without right paren",
            },
            {
                "get_node": lambda: cst.Integer("5", rpar=(cst.RightParen(),)),
                "expected_re": "right paren without left paren",
            },
            {
                "get_node": lambda: cst.Float("5.5", lpar=(cst.LeftParen(),)),
                "expected_re": "left paren without right paren",
            },
            {
                "get_node": lambda: cst.Float("5.5", rpar=(cst.RightParen(),)),
                "expected_re": "right paren without left paren",
            },
            {
                "get_node": (lambda: cst.Imaginary("5j", lpar=(cst.LeftParen(),))),
                "expected_re": "left paren without right paren",
            },
            {
                "get_node": (lambda: cst.Imaginary("5j", rpar=(cst.RightParen(),))),
                "expected_re": "right paren without left paren",
            },
            {
                "get_node": (lambda: cst.Integer("5", lpar=(cst.LeftParen(),))),
                "expected_re": "left paren without right paren",
            },
            {
                "get_node": (lambda: cst.Integer("5", rpar=(cst.RightParen(),))),
                "expected_re": "right paren without left paren",
            },
            {
                "get_node": (
                    lambda: cst.SimpleString("'foo'", lpar=(cst.LeftParen(),))
                ),
                "expected_re": "left paren without right paren",
            },
            {
                "get_node": (
                    lambda: cst.SimpleString("'foo'", rpar=(cst.RightParen(),))
                ),
                "expected_re": "right paren without left paren",
            },
            {
                "get_node": (
                    lambda: cst.FormattedString(parts=(), lpar=(cst.LeftParen(),))
                ),
                "expected_re": "left paren without right paren",
            },
            {
                "get_node": (
                    lambda: cst.FormattedString(parts=(), rpar=(cst.RightParen(),))
                ),
                "expected_re": "right paren without left paren",
            },
            {
                "get_node": (
                    lambda: cst.ConcatenatedString(
                        cst.SimpleString("'foo'"),
                        cst.SimpleString("'foo'"),
                        lpar=(cst.LeftParen(),),
                    )
                ),
                "expected_re": "left paren without right paren",
            },
            {
                "get_node": (
                    lambda: cst.ConcatenatedString(
                        cst.SimpleString("'foo'"),
                        cst.SimpleString("'foo'"),
                        rpar=(cst.RightParen(),),
                    )
                ),
                "expected_re": "right paren without left paren",
            },
            # Node-specific rules
            {
                "get_node": (lambda: cst.Name("")),
                "expected_re": "empty name identifier",
            },
            {
                "get_node": (lambda: cst.Name(r"\/")),
                "expected_re": "not a valid identifier",
            },
            {
                "get_node": (lambda: cst.Integer("")),
                "expected_re": "not a valid integer",
            },
            {
                "get_node": (lambda: cst.Integer("012345")),
                "expected_re": "not a valid integer",
            },
            {
                "get_node": (lambda: cst.Integer("012345")),
                "expected_re": "not a valid integer",
            },
            {
                "get_node": (lambda: cst.Integer("_12345")),
                "expected_re": "not a valid integer",
            },
            {
                "get_node": (lambda: cst.Integer("0b2")),
                "expected_re": "not a valid integer",
            },
            {
                "get_node": (lambda: cst.Integer("0o8")),
                "expected_re": "not a valid integer",
            },
            {
                "get_node": (lambda: cst.Integer("0xg")),
                "expected_re": "not a valid integer",
            },
            {
                "get_node": (lambda: cst.Integer("123.45")),
                "expected_re": "not a valid integer",
            },
            {
                "get_node": (lambda: cst.Integer("12345j")),
                "expected_re": "not a valid integer",
            },
            {
                "get_node": (lambda: cst.Float("12.3.45")),
                "expected_re": "not a valid float",
            },
            {"get_node": (lambda: cst.Float("12")), "expected_re": "not a valid float"},
            {
                "get_node": (lambda: cst.Float("12.3j")),
                "expected_re": "not a valid float",
            },
            {
                "get_node": (lambda: cst.Imaginary("_12345j")),
                "expected_re": "not a valid imaginary",
            },
            {
                "get_node": (lambda: cst.Imaginary("0b0j")),
                "expected_re": "not a valid imaginary",
            },
            {
                "get_node": (lambda: cst.Imaginary("0o0j")),
                "expected_re": "not a valid imaginary",
            },
            {
                "get_node": (lambda: cst.Imaginary("0x0j")),
                "expected_re": "not a valid imaginary",
            },
            {
                "get_node": (lambda: cst.SimpleString('wee""')),
                "expected_re": "Invalid string prefix",
            },
            {
                "get_node": (lambda: cst.SimpleString("'")),
                "expected_re": "must have enclosing quotes",
            },
            {
                "get_node": (lambda: cst.SimpleString('"')),
                "expected_re": "must have enclosing quotes",
            },
            {
                "get_node": (lambda: cst.SimpleString("\"'")),
                "expected_re": "must have matching enclosing quotes",
            },
            {
                "get_node": (lambda: cst.SimpleString("")),
                "expected_re": "must have enclosing quotes",
            },
            {
                "get_node": (lambda: cst.SimpleString("'bla")),
                "expected_re": "must have matching enclosing quotes",
            },
            {
                "get_node": (lambda: cst.SimpleString("f''")),
                "expected_re": "Invalid string prefix",
            },
            {
                "get_node": (lambda: cst.SimpleString("'''bla''")),
                "expected_re": "must have matching enclosing quotes",
            },
            {
                "get_node": (lambda: cst.SimpleString("'''bla\"\"\"")),
                "expected_re": "must have matching enclosing quotes",
            },
            {
                "get_node": (lambda: cst.FormattedString(start="'", parts=(), end="'")),
                "expected_re": "Invalid f-string prefix",
            },
            {
                "get_node": (
                    lambda: cst.FormattedString(start="f'", parts=(), end='"')
                ),
                "expected_re": "must have matching enclosing quotes",
            },
            {
                "get_node": (
                    lambda: cst.ConcatenatedString(
                        cst.SimpleString(
                            '"ab"', lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                        ),
                        cst.SimpleString('"c"'),
                    )
                ),
                "expected_re": "Cannot concatenate parenthesized",
            },
            {
                "get_node": (
                    lambda: cst.ConcatenatedString(
                        cst.SimpleString('"ab"'),
                        cst.SimpleString(
                            '"c"', lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                        ),
                    )
                ),
                "expected_re": "Cannot concatenate parenthesized",
            },
            {
                "get_node": (
                    lambda: cst.ConcatenatedString(
                        cst.SimpleString('"ab"'), cst.SimpleString('b"c"')
                    )
                ),
                "expected_re": "Cannot concatenate string and bytes",
            },
            # This isn't valid code: `"a" inb"abc"`
            {
                "get_node": (
                    lambda: cst.Comparison(
                        cst.SimpleString('"a"'),
                        [
                            cst.ComparisonTarget(
                                cst.In(whitespace_after=cst.SimpleWhitespace("")),
                                cst.SimpleString('b"abc"'),
                            )
                        ],
                    )
                ),
                "expected_re": "Must have at least one space around comparison operator.",
            },
            # Also not valid: `"a" in b"a"b"bc"`
            {
                "get_node": (
                    lambda: cst.Comparison(
                        cst.SimpleString('"a"'),
                        [
                            cst.ComparisonTarget(
                                cst.In(whitespace_after=cst.SimpleWhitespace("")),
                                cst.ConcatenatedString(
                                    cst.SimpleString('b"a"'), cst.SimpleString('b"bc"')
                                ),
                            )
                        ],
                    )
                ),
                "expected_re": "Must have at least one space around comparison operator.",
            },
        )
    )
    def test_invalid(self, **kwargs: Any) -> None:
        self.assert_invalid(**kwargs)

    @data_provider(
        (
            {
                "code": "u'x'",
                "parser": parse_expression_as(python_version="3.3"),
                "expect_success": True,
            },
            {
                "code": "u'x'",
                "parser": parse_expression_as(python_version="3.1"),
                "expect_success": False,
            },
        )
    )
    def test_versions(self, **kwargs: Any) -> None:
        if is_native() and not kwargs.get("expect_success", True):
            self.skipTest("parse errors are disabled for native parser")
        self.assert_parses(**kwargs)


class StringHelperTest(CSTNodeTest):
    def test_string_prefix_and_quotes(self) -> None:
        """
        Test our helpers out for various strings.
        """
        emptybytestring = cst.ensure_type(parse_expression('b""'), cst.SimpleString)
        bytestring = cst.ensure_type(parse_expression('b"abc"'), cst.SimpleString)
        multilinestring = cst.ensure_type(parse_expression('""""""'), cst.SimpleString)
        formatstring = cst.ensure_type(parse_expression('f""""""'), cst.FormattedString)

        self.assertEqual(emptybytestring.prefix, "b")
        self.assertEqual(emptybytestring.quote, '"')
        self.assertEqual(emptybytestring.raw_value, "")
        self.assertEqual(bytestring.prefix, "b")
        self.assertEqual(bytestring.quote, '"')
        self.assertEqual(bytestring.raw_value, "abc")
        self.assertEqual(multilinestring.prefix, "")
        self.assertEqual(multilinestring.quote, '"""')
        self.assertEqual(multilinestring.raw_value, "")
        self.assertEqual(formatstring.prefix, "f")
        self.assertEqual(formatstring.quote, '"""')
