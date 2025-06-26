# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Type, Union

import libcst as cst
from libcst import parse_module, RemovalSentinel
from libcst._nodes.tests.base import CSTNodeTest
from libcst._types import CSTNodeT
from libcst._visitors import CSTTransformer
from libcst.testing.utils import data_provider


class IfStatementRemovalVisitor(CSTTransformer):
    def on_leave(
        self, original_node: CSTNodeT, updated_node: CSTNodeT
    ) -> Union[CSTNodeT, RemovalSentinel]:
        if isinstance(updated_node, cst.If):
            return cst.RemoveFromParent()
        else:
            return updated_node


class ContinueStatementRemovalVisitor(CSTTransformer):
    def on_leave(
        self, original_node: CSTNodeT, updated_node: CSTNodeT
    ) -> Union[CSTNodeT, RemovalSentinel]:
        if isinstance(updated_node, cst.Continue):
            return cst.RemoveFromParent()
        else:
            return updated_node


class SpecificImportRemovalVisitor(CSTTransformer):
    def on_leave(
        self, original_node: CSTNodeT, updated_node: CSTNodeT
    ) -> Union[cst.Import, cst.ImportFrom, CSTNodeT, RemovalSentinel]:
        if isinstance(updated_node, cst.Import):
            for alias in updated_node.names:
                name = alias.name
                if isinstance(name, cst.Name) and name.value == "b":
                    return cst.RemoveFromParent()
        elif isinstance(updated_node, cst.ImportFrom):
            module = updated_node.module
            if isinstance(module, cst.Name) and module.value == "e":
                return cst.RemoveFromParent()
        return updated_node


class RemovalBehavior(CSTNodeTest):
    @data_provider(
        (
            # Top of module doesn't require a pass, empty code is valid.
            ("continue", "", ContinueStatementRemovalVisitor),
            ("if condition: print('hello world')", "", IfStatementRemovalVisitor),
            # Verify behavior within an indented block.
            (
                "while True:\n    continue",
                "while True:\n    pass",
                ContinueStatementRemovalVisitor,
            ),
            (
                "while True:\n    if condition: print('hello world')",
                "while True:\n    pass",
                IfStatementRemovalVisitor,
            ),
            # Verify behavior within a simple statement suite.
            (
                "while True: continue",
                "while True: pass",
                ContinueStatementRemovalVisitor,
            ),
            # Verify with some imports
            (
                "import a\nimport b\n\nfrom c import d\nfrom e import f",
                "import a\n\nfrom c import d",
                SpecificImportRemovalVisitor,
            ),
            # Verify only one pass is generated even if we remove multiple statements
            (
                "while True:\n    continue\ncontinue",
                "while True:\n    pass",
                ContinueStatementRemovalVisitor,
            ),
            (
                "while True: continue ; continue",
                "while True: pass",
                ContinueStatementRemovalVisitor,
            ),
        )
    )
    def test_removal_pass_behavior(
        self, before: str, after: str, visitor: Type[CSTTransformer]
    ) -> None:
        if before.endswith("\n") or after.endswith("\n"):
            raise ValueError("Test cases should not be newline-terminated!")

        # Test doesn't have newline termination case
        before_module = parse_module(before)
        after_module = before_module.visit(visitor())
        self.assertEqual(after, after_module.code)

        # Test does have newline termination case
        before_module = parse_module(before + "\n")
        after_module = before_module.visit(visitor())
        self.assertEqual(after + "\n", after_module.code)
