# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Type, Union

import libcst as cst
from libcst import FlattenSentinel, parse_expression, parse_module, RemovalSentinel
from libcst._nodes.tests.base import CSTNodeTest
from libcst._types import CSTNodeT
from libcst._visitors import CSTTransformer
from libcst.testing.utils import data_provider


class InsertPrintBeforeReturn(CSTTransformer):
    def leave_Return(
        self, original_node: cst.Return, updated_node: cst.Return
    ) -> Union[cst.Return, RemovalSentinel, FlattenSentinel[cst.BaseSmallStatement]]:
        return FlattenSentinel(
            [
                cst.Expr(parse_expression("print('returning')")),
                updated_node,
            ]
        )


class FlattenLines(CSTTransformer):
    def on_leave(
        self, original_node: CSTNodeT, updated_node: CSTNodeT
    ) -> Union[CSTNodeT, RemovalSentinel, FlattenSentinel[cst.SimpleStatementLine]]:
        if isinstance(updated_node, cst.SimpleStatementLine):
            return FlattenSentinel(
                [
                    cst.SimpleStatementLine(
                        [stmt.with_changes(semicolon=cst.MaybeSentinel.DEFAULT)]
                    )
                    for stmt in updated_node.body
                ]
            )
        else:
            return updated_node


class RemoveReturnWithEmpty(CSTTransformer):
    def leave_Return(
        self, original_node: cst.Return, updated_node: cst.Return
    ) -> Union[cst.Return, RemovalSentinel, FlattenSentinel[cst.BaseSmallStatement]]:
        return FlattenSentinel([])


class FlattenBehavior(CSTNodeTest):
    @data_provider(
        (
            ("return", "print('returning'); return", InsertPrintBeforeReturn),
            (
                "print('returning'); return",
                "print('returning')\nreturn",
                FlattenLines,
            ),
            (
                "print('returning')\nreturn",
                "print('returning')",
                RemoveReturnWithEmpty,
            ),
        )
    )
    def test_flatten_pass_behavior(
        self, before: str, after: str, visitor: Type[CSTTransformer]
    ) -> None:
        # Test doesn't have newline termination case
        before_module = parse_module(before)
        after_module = before_module.visit(visitor())
        self.assertEqual(after, after_module.code)

        # Test does have newline termination case
        before_module = parse_module(before + "\n")
        after_module = before_module.visit(visitor())
        self.assertEqual(after + "\n", after_module.code)
