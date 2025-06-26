# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import List

import libcst as cst
from libcst import CSTTransformer, CSTVisitor, parse_module
from libcst.testing.utils import UnitTest


class VisitorTest(UnitTest):
    def test_visitor(self) -> None:
        class SomeVisitor(CSTVisitor):
            def __init__(self) -> None:
                self.visit_order: List[str] = []

            def visit_If(self, node: cst.If) -> None:
                self.visit_order.append("visit_If")

            def leave_If(self, original_node: cst.If) -> None:
                self.visit_order.append("leave_If")

            def visit_If_test(self, node: cst.If) -> None:
                self.visit_order.append("visit_If_test")

            def leave_If_test(self, node: cst.If) -> None:
                self.visit_order.append("leave_If_test")

            def visit_Name(self, node: cst.Name) -> None:
                self.visit_order.append("visit_Name")

            def leave_Name(self, original_node: cst.Name) -> None:
                self.visit_order.append("leave_Name")

        # Create and visit a simple module.
        module = parse_module("if True:\n    pass")
        visitor = SomeVisitor()
        module.visit(visitor)

        # Check that visits worked.
        self.assertEqual(
            visitor.visit_order,
            [
                "visit_If",
                "visit_If_test",
                "visit_Name",
                "leave_Name",
                "leave_If_test",
                "leave_If",
            ],
        )

    def test_transformer(self) -> None:
        class SomeTransformer(CSTTransformer):
            def __init__(self) -> None:
                self.visit_order: List[str] = []

            def visit_If(self, node: cst.If) -> None:
                self.visit_order.append("visit_If")

            def leave_If(self, original_node: cst.If, updated_node: cst.If) -> cst.If:
                self.visit_order.append("leave_If")
                return updated_node

            def visit_If_test(self, node: cst.If) -> None:
                self.visit_order.append("visit_If_test")

            def leave_If_test(self, node: cst.If) -> None:
                self.visit_order.append("leave_If_test")

            def visit_Name(self, node: cst.Name) -> None:
                self.visit_order.append("visit_Name")

            def leave_Name(
                self, original_node: cst.Name, updated_node: cst.Name
            ) -> cst.Name:
                self.visit_order.append("leave_Name")
                return updated_node

        # Create and visit a simple module.
        module = parse_module("if True:\n    pass")
        transformer = SomeTransformer()
        module.visit(transformer)

        # Check that visits worked.
        self.assertEqual(
            transformer.visit_order,
            [
                "visit_If",
                "visit_If_test",
                "visit_Name",
                "leave_Name",
                "leave_If_test",
                "leave_If",
            ],
        )
