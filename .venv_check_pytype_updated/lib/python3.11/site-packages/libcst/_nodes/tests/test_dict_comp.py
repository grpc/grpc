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


class DictCompTest(CSTNodeTest):
    @data_provider(
        [
            # simple DictComp
            {
                "node": cst.DictComp(
                    cst.Name("k"),
                    cst.Name("v"),
                    cst.CompFor(target=cst.Name("a"), iter=cst.Name("b")),
                ),
                "code": "{k: v for a in b}",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 0), (1, 17)),
            },
            # non-trivial keys & values in DictComp
            {
                "node": cst.DictComp(
                    cst.BinaryOperation(cst.Name("k1"), cst.Add(), cst.Name("k2")),
                    cst.BinaryOperation(cst.Name("v1"), cst.Add(), cst.Name("v2")),
                    cst.CompFor(target=cst.Name("a"), iter=cst.Name("b")),
                ),
                "code": "{k1 + k2: v1 + v2 for a in b}",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 0), (1, 29)),
            },
            # custom whitespace around colon
            {
                "node": cst.DictComp(
                    cst.Name("k"),
                    cst.Name("v"),
                    cst.CompFor(target=cst.Name("a"), iter=cst.Name("b")),
                    whitespace_before_colon=cst.SimpleWhitespace("\t"),
                    whitespace_after_colon=cst.SimpleWhitespace("\t\t"),
                ),
                "code": "{k\t:\t\tv for a in b}",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 0), (1, 19)),
            },
            # custom whitespace inside braces
            {
                "node": cst.DictComp(
                    cst.Name("k"),
                    cst.Name("v"),
                    cst.CompFor(target=cst.Name("a"), iter=cst.Name("b")),
                    lbrace=cst.LeftCurlyBrace(
                        whitespace_after=cst.SimpleWhitespace("\t")
                    ),
                    rbrace=cst.RightCurlyBrace(
                        whitespace_before=cst.SimpleWhitespace("\t\t")
                    ),
                ),
                "code": "{\tk: v for a in b\t\t}",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 0), (1, 20)),
            },
            # parenthesis
            {
                "node": cst.DictComp(
                    cst.Name("k"),
                    cst.Name("v"),
                    cst.CompFor(target=cst.Name("a"), iter=cst.Name("b")),
                    lpar=[cst.LeftParen()],
                    rpar=[cst.RightParen()],
                ),
                "code": "({k: v for a in b})",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 1), (1, 18)),
            },
            # missing spaces around DictComp is always okay
            {
                "node": cst.DictComp(
                    cst.Name("a"),
                    cst.Name("b"),
                    cst.CompFor(
                        target=cst.Name("c"),
                        iter=cst.DictComp(
                            cst.Name("d"),
                            cst.Name("e"),
                            cst.CompFor(target=cst.Name("f"), iter=cst.Name("g")),
                        ),
                        ifs=[
                            cst.CompIf(
                                cst.Name("h"),
                                whitespace_before=cst.SimpleWhitespace(""),
                            )
                        ],
                        whitespace_after_in=cst.SimpleWhitespace(""),
                    ),
                ),
                "code": "{a: b for c in{d: e for f in g}if h}",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 0), (1, 36)),
            },
            # no whitespace before `for` clause
            {
                "node": cst.DictComp(
                    cst.Name("k"),
                    cst.Name("v", lpar=[cst.LeftParen()], rpar=[cst.RightParen()]),
                    cst.CompFor(
                        target=cst.Name("a"),
                        iter=cst.Name("b"),
                        whitespace_before=cst.SimpleWhitespace(""),
                    ),
                ),
                "code": "{k: (v)for a in b}",
                "parser": parse_expression,
                "expected_position": CodeRange((1, 0), (1, 18)),
            },
        ]
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        [
            # unbalanced DictComp
            {
                "get_node": lambda: cst.DictComp(
                    cst.Name("k"),
                    cst.Name("v"),
                    cst.CompFor(target=cst.Name("a"), iter=cst.Name("b")),
                    lpar=[cst.LeftParen()],
                ),
                "expected_re": "left paren without right paren",
            },
            # invalid whitespace before for/async
            {
                "get_node": lambda: cst.DictComp(
                    cst.Name("k"),
                    cst.Name("v"),
                    cst.CompFor(
                        target=cst.Name("a"),
                        iter=cst.Name("b"),
                        whitespace_before=cst.SimpleWhitespace(""),
                    ),
                ),
                "expected_re": "Must have at least one space before 'for' keyword.",
            },
            {
                "get_node": lambda: cst.DictComp(
                    cst.Name("k"),
                    cst.Name("v"),
                    cst.CompFor(
                        target=cst.Name("a"),
                        iter=cst.Name("b"),
                        asynchronous=cst.Asynchronous(),
                        whitespace_before=cst.SimpleWhitespace(""),
                    ),
                ),
                "expected_re": "Must have at least one space before 'async' keyword.",
            },
        ]
    )
    def test_invalid(self, **kwargs: Any) -> None:
        self.assert_invalid(**kwargs)
