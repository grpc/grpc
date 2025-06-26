# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
from textwrap import dedent
from typing import Set

import libcst as cst
from libcst.testing.utils import data_provider, UnitTest


class DeepCloneTest(UnitTest):
    @data_provider(
        (
            # Simple program
            (
                """
                foo = 'toplevel'
                fn1(foo)
                fn2(foo)
                def fn_def():
                    foo = 'shadow'
                    fn3(foo)
            """,
            ),
        )
    )
    def test_deep_clone(self, code: str) -> None:
        test_case = self

        class NodeGatherVisitor(cst.CSTVisitor):
            def __init__(self) -> None:
                self.nodes: Set[int] = set()

            def on_visit(self, node: cst.CSTNode) -> bool:
                self.nodes.add(id(node))
                return True

        class NodeVerifyVisitor(cst.CSTVisitor):
            def __init__(self, nodes: Set[int]) -> None:
                self.nodes = nodes

            def on_visit(self, node: cst.CSTNode) -> bool:
                test_case.assertFalse(
                    id(node) in self.nodes, f"Node {node} was not cloned properly!"
                )
                return True

        module = cst.parse_module(dedent(code))
        gatherer = NodeGatherVisitor()
        module.visit(gatherer)
        new_module = module.deep_clone()
        self.assertTrue(module.deep_equals(new_module))
        new_module.visit(NodeVerifyVisitor(gatherer.nodes))
