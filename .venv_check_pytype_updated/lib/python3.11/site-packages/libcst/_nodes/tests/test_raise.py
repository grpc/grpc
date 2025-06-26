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


class RaiseConstructionTest(CSTNodeTest):
    @data_provider(
        (
            # Simple raise
            {"node": cst.Raise(), "code": "raise"},
            # Raise exception
            {
                "node": cst.Raise(cst.Call(cst.Name("Exception"))),
                "code": "raise Exception()",
                "expected_position": CodeRange((1, 0), (1, 17)),
            },
            # Raise exception from cause
            {
                "node": cst.Raise(
                    cst.Call(cst.Name("Exception")), cst.From(cst.Name("cause"))
                ),
                "code": "raise Exception() from cause",
            },
            # Whitespace oddities test
            {
                "node": cst.Raise(
                    cst.Call(
                        cst.Name("Exception"),
                        lpar=(cst.LeftParen(),),
                        rpar=(cst.RightParen(),),
                    ),
                    cst.From(
                        cst.Name(
                            "cause", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                        ),
                        whitespace_before_from=cst.SimpleWhitespace(""),
                        whitespace_after_from=cst.SimpleWhitespace(""),
                    ),
                    whitespace_after_raise=cst.SimpleWhitespace(""),
                ),
                "code": "raise(Exception())from(cause)",
                "expected_position": CodeRange((1, 0), (1, 29)),
            },
            {
                "node": cst.Raise(
                    cst.Call(cst.Name("Exception")),
                    cst.From(
                        cst.Name("cause"),
                        whitespace_before_from=cst.SimpleWhitespace(""),
                    ),
                ),
                "code": "raise Exception()from cause",
                "expected_position": CodeRange((1, 0), (1, 27)),
            },
            # Whitespace rendering test
            {
                "node": cst.Raise(
                    exc=cst.Call(cst.Name("Exception")),
                    cause=cst.From(
                        cst.Name("cause"),
                        whitespace_before_from=cst.SimpleWhitespace("  "),
                        whitespace_after_from=cst.SimpleWhitespace("  "),
                    ),
                    whitespace_after_raise=cst.SimpleWhitespace("  "),
                ),
                "code": "raise  Exception()  from  cause",
                "expected_position": CodeRange((1, 0), (1, 31)),
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            # Validate construction
            {
                "get_node": lambda: cst.Raise(cause=cst.From(cst.Name("cause"))),
                "expected_re": "Must have an 'exc' when specifying 'clause'. on Raise",
            },
            # Validate whitespace handling
            {
                "get_node": lambda: cst.Raise(
                    cst.Call(cst.Name("Exception")),
                    whitespace_after_raise=cst.SimpleWhitespace(""),
                ),
                "expected_re": "Must have at least one space after 'raise'",
            },
            {
                "get_node": lambda: cst.Raise(
                    cst.Name("exc"),
                    cst.From(
                        cst.Name("cause"),
                        whitespace_before_from=cst.SimpleWhitespace(""),
                    ),
                ),
                "expected_re": "Must have at least one space before 'from'",
            },
            {
                "get_node": lambda: cst.Raise(
                    cst.Name("exc"),
                    cst.From(
                        cst.Name("cause"),
                        whitespace_after_from=cst.SimpleWhitespace(""),
                    ),
                ),
                "expected_re": "Must have at least one space after 'from'",
            },
        )
    )
    def test_invalid(self, **kwargs: Any) -> None:
        self.assert_invalid(**kwargs)


class RaiseParsingTest(CSTNodeTest):
    @data_provider(
        (
            # Simple raise
            {"node": cst.Raise(), "code": "raise"},
            # Raise exception
            {
                "node": cst.Raise(
                    cst.Call(cst.Name("Exception")),
                    whitespace_after_raise=cst.SimpleWhitespace(" "),
                ),
                "code": "raise Exception()",
            },
            # Raise exception from cause
            {
                "node": cst.Raise(
                    cst.Call(cst.Name("Exception")),
                    cst.From(
                        cst.Name("cause"),
                        whitespace_before_from=cst.SimpleWhitespace(" "),
                        whitespace_after_from=cst.SimpleWhitespace(" "),
                    ),
                    whitespace_after_raise=cst.SimpleWhitespace(" "),
                ),
                "code": "raise Exception() from cause",
            },
            # Whitespace oddities test
            {
                "node": cst.Raise(
                    cst.Call(
                        cst.Name("Exception"),
                        lpar=(cst.LeftParen(),),
                        rpar=(cst.RightParen(),),
                    ),
                    cst.From(
                        cst.Name(
                            "cause", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                        ),
                        whitespace_before_from=cst.SimpleWhitespace(""),
                        whitespace_after_from=cst.SimpleWhitespace(""),
                    ),
                    whitespace_after_raise=cst.SimpleWhitespace(""),
                ),
                "code": "raise(Exception())from(cause)",
            },
            {
                "node": cst.Raise(
                    cst.Call(cst.Name("Exception")),
                    cst.From(
                        cst.Name("cause"),
                        whitespace_before_from=cst.SimpleWhitespace(""),
                        whitespace_after_from=cst.SimpleWhitespace(" "),
                    ),
                    whitespace_after_raise=cst.SimpleWhitespace(" "),
                ),
                "code": "raise Exception()from cause",
            },
            # Whitespace rendering test
            {
                "node": cst.Raise(
                    exc=cst.Call(cst.Name("Exception")),
                    cause=cst.From(
                        cst.Name("cause"),
                        whitespace_before_from=cst.SimpleWhitespace("  "),
                        whitespace_after_from=cst.SimpleWhitespace("  "),
                    ),
                    whitespace_after_raise=cst.SimpleWhitespace("  "),
                ),
                "code": "raise  Exception()  from  cause",
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
