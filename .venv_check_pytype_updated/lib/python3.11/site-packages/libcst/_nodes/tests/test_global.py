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


class GlobalConstructionTest(CSTNodeTest):
    @data_provider(
        (
            # Single global statement
            {"node": cst.Global((cst.NameItem(cst.Name("a")),)), "code": "global a"},
            # Multiple entries in global statement
            {
                "node": cst.Global(
                    (cst.NameItem(cst.Name("a")), cst.NameItem(cst.Name("b")))
                ),
                "code": "global a, b",
            },
            # Whitespace rendering test
            {
                "node": cst.Global(
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
                    whitespace_after_global=cst.SimpleWhitespace("  "),
                ),
                "code": "global  a  ,  b",
                "expected_position": CodeRange((1, 0), (1, 15)),
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            # Validate construction
            {
                "get_node": lambda: cst.Global(()),
                "expected_re": "A Global statement must have at least one NameItem",
            },
            # Validate whitespace handling
            {
                "get_node": lambda: cst.Global(
                    (cst.NameItem(cst.Name("a")),),
                    whitespace_after_global=cst.SimpleWhitespace(""),
                ),
                "expected_re": "Must have at least one space after 'global' keyword",
            },
            # Validate comma handling
            {
                "get_node": lambda: cst.Global(
                    (cst.NameItem(cst.Name("a"), comma=cst.Comma()),)
                ),
                "expected_re": "The last NameItem in a Global cannot have a trailing comma",
            },
            # Validate paren handling
            {
                "get_node": lambda: cst.Global(
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


class GlobalParsingTest(CSTNodeTest):
    @data_provider(
        (
            # Single global statement
            {"node": cst.Global((cst.NameItem(cst.Name("a")),)), "code": "global a"},
            # Multiple entries in global statement
            {
                "node": cst.Global(
                    (
                        cst.NameItem(
                            cst.Name("a"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.NameItem(cst.Name("b")),
                    )
                ),
                "code": "global a, b",
            },
            # Whitespace rendering test
            {
                "node": cst.Global(
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
                    whitespace_after_global=cst.SimpleWhitespace("  "),
                ),
                "code": "global  a  ,  b",
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
