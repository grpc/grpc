# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any

import libcst as cst
from libcst import parse_statement
from libcst._nodes.tests.base import CSTNodeTest
from libcst.helpers import ensure_type
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider


class NonlocalConstructionTest(CSTNodeTest):
    @data_provider(
        (
            # Single nonlocal statement
            {
                "node": cst.Nonlocal((cst.NameItem(cst.Name("a")),)),
                "code": "nonlocal a",
            },
            # Multiple entries in nonlocal statement
            {
                "node": cst.Nonlocal(
                    (cst.NameItem(cst.Name("a")), cst.NameItem(cst.Name("b")))
                ),
                "code": "nonlocal a, b",
                "expected_position": CodeRange((1, 0), (1, 13)),
            },
            # Whitespace rendering test
            {
                "node": cst.Nonlocal(
                    (
                        cst.NameItem(
                            cst.Name("a"),
                            comma=cst.Comma(
                                whitespace_before=cst.SimpleWhitespace("  "),
                                whitespace_after=cst.SimpleWhitespace("  "),
                            ),
                        ),
                        cst.NameItem(cst.Name("b")),
                    ),
                    whitespace_after_nonlocal=cst.SimpleWhitespace("  "),
                ),
                "code": "nonlocal  a  ,  b",
                "expected_position": CodeRange((1, 0), (1, 17)),
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            # Validate construction
            {
                "get_node": lambda: cst.Nonlocal(()),
                "expected_re": "A Nonlocal statement must have at least one NameItem",
            },
            # Validate whitespace handling
            {
                "get_node": lambda: cst.Nonlocal(
                    (cst.NameItem(cst.Name("a")),),
                    whitespace_after_nonlocal=cst.SimpleWhitespace(""),
                ),
                "expected_re": "Must have at least one space after 'nonlocal' keyword",
            },
            # Validate comma handling
            {
                "get_node": lambda: cst.Nonlocal(
                    (cst.NameItem(cst.Name("a"), comma=cst.Comma()),)
                ),
                "expected_re": "The last NameItem in a Nonlocal cannot have a trailing comma",
            },
            # Validate paren handling
            {
                "get_node": lambda: cst.Nonlocal(
                    (
                        cst.NameItem(
                            cst.Name(
                                "a", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                            )
                        ),
                    )
                ),
                "expected_re": "Cannot have parens around names in NameItem",
            },
        )
    )
    def test_invalid(self, **kwargs: Any) -> None:
        self.assert_invalid(**kwargs)


class NonlocalParsingTest(CSTNodeTest):
    @data_provider(
        (
            # Single nonlocal statement
            {
                "node": cst.Nonlocal((cst.NameItem(cst.Name("a")),)),
                "code": "nonlocal a",
            },
            # Multiple entries in nonlocal statement
            {
                "node": cst.Nonlocal(
                    (
                        cst.NameItem(
                            cst.Name("a"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.NameItem(cst.Name("b")),
                    )
                ),
                "code": "nonlocal a, b",
            },
            # Whitespace rendering test
            {
                "node": cst.Nonlocal(
                    (
                        cst.NameItem(
                            cst.Name("a"),
                            comma=cst.Comma(
                                whitespace_before=cst.SimpleWhitespace("  "),
                                whitespace_after=cst.SimpleWhitespace("  "),
                            ),
                        ),
                        cst.NameItem(cst.Name("b")),
                    ),
                    whitespace_after_nonlocal=cst.SimpleWhitespace("  "),
                ),
                "code": "nonlocal  a  ,  b",
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(
            parser=lambda code: ensure_type(
                parse_statement(code), cst.SimpleStatementLine
            ).body[0],
            **kwargs,
        )
