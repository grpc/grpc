# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any

import libcst as cst
from libcst._nodes.tests.base import (
    CSTNodeTest,
    parse_expression_as,
    parse_statement_as,
)
from libcst._parser.entrypoints import is_native
from libcst.testing.utils import data_provider


class NamedExprTest(CSTNodeTest):
    @data_provider(
        (
            {
                "node": cst.BinaryOperation(
                    left=cst.Name("a"),
                    operator=cst.MatrixMultiply(),
                    right=cst.Name("b"),
                ),
                "code": "a @ b",
                "parser": parse_expression_as(python_version="3.8"),
            },
            {
                "node": cst.SimpleStatementLine(
                    body=(
                        cst.AugAssign(
                            target=cst.Name("a"),
                            operator=cst.MatrixMultiplyAssign(),
                            value=cst.Name("b"),
                        ),
                    ),
                ),
                "code": "a @= b\n",
                "parser": parse_statement_as(python_version="3.8"),
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            {
                "code": "a @ b",
                "parser": parse_expression_as(python_version="3.6"),
                "expect_success": True,
            },
            {
                "code": "a @ b",
                "parser": parse_expression_as(python_version="3.3"),
                "expect_success": False,
            },
            {
                "code": "a @= b",
                "parser": parse_statement_as(python_version="3.6"),
                "expect_success": True,
            },
            {
                "code": "a @= b",
                "parser": parse_statement_as(python_version="3.3"),
                "expect_success": False,
            },
        )
    )
    def test_versions(self, **kwargs: Any) -> None:
        if is_native() and not kwargs.get("expect_success", True):
            self.skipTest("parse errors are disabled for native parser")
        self.assert_parses(**kwargs)
