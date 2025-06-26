# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
from collections import Counter
from textwrap import dedent

import libcst as cst
from libcst.testing.utils import data_provider, UnitTest


class DuplicateLeafNodeTest(UnitTest):
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
    def test_tokenize(self, code: str) -> None:
        test_case = self

        class CountVisitor(cst.CSTVisitor):
            def __init__(self) -> None:
                self.count = Counter()
                self.nodes = {}

            def on_visit(self, node: cst.CSTNode) -> bool:
                self.count[id(node)] += 1
                test_case.assertTrue(
                    self.count[id(node)] == 1,
                    f"Node duplication detected between {node} and {self.nodes.get(id(node))}",
                )
                self.nodes[id(node)] = node
                return True

        module = cst.parse_module(dedent(code))
        module.visit(CountVisitor())
