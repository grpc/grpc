# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any

import libcst as cst
from libcst._nodes.tests.base import CSTNodeTest
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider


class SmallStatementTest(CSTNodeTest):
    @data_provider(
        (
            {"node": cst.Pass(), "code": "pass"},
            {"node": cst.Pass(semicolon=cst.Semicolon()), "code": "pass;"},
            {
                "node": cst.Pass(
                    semicolon=cst.Semicolon(
                        whitespace_before=cst.SimpleWhitespace("  "),
                        whitespace_after=cst.SimpleWhitespace("    "),
                    )
                ),
                "code": "pass  ;    ",
                "expected_position": CodeRange((1, 0), (1, 4)),
            },
            {"node": cst.Continue(), "code": "continue"},
            {"node": cst.Continue(semicolon=cst.Semicolon()), "code": "continue;"},
            {
                "node": cst.Continue(
                    semicolon=cst.Semicolon(
                        whitespace_before=cst.SimpleWhitespace("  "),
                        whitespace_after=cst.SimpleWhitespace("    "),
                    )
                ),
                "code": "continue  ;    ",
                "expected_position": CodeRange((1, 0), (1, 8)),
            },
            {"node": cst.Break(), "code": "break"},
            {"node": cst.Break(semicolon=cst.Semicolon()), "code": "break;"},
            {
                "node": cst.Break(
                    semicolon=cst.Semicolon(
                        whitespace_before=cst.SimpleWhitespace("  "),
                        whitespace_after=cst.SimpleWhitespace("    "),
                    )
                ),
                "code": "break  ;    ",
                "expected_position": CodeRange((1, 0), (1, 5)),
            },
            {
                "node": cst.Expr(
                    cst.BinaryOperation(cst.Name("x"), cst.Add(), cst.Name("y"))
                ),
                "code": "x + y",
            },
            {
                "node": cst.Expr(
                    cst.BinaryOperation(cst.Name("x"), cst.Add(), cst.Name("y")),
                    semicolon=cst.Semicolon(),
                ),
                "code": "x + y;",
            },
            {
                "node": cst.Expr(
                    cst.BinaryOperation(cst.Name("x"), cst.Add(), cst.Name("y")),
                    semicolon=cst.Semicolon(
                        whitespace_before=cst.SimpleWhitespace("  "),
                        whitespace_after=cst.SimpleWhitespace("    "),
                    ),
                ),
                "code": "x + y  ;    ",
                "expected_position": CodeRange((1, 0), (1, 5)),
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)
