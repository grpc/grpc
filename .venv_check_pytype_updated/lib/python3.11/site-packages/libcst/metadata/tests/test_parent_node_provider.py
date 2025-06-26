# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


from textwrap import dedent

import libcst as cst
from libcst.metadata import MetadataWrapper, ParentNodeProvider
from libcst.testing.utils import data_provider, UnitTest


class DependentVisitor(cst.CSTVisitor):
    METADATA_DEPENDENCIES = (ParentNodeProvider,)

    def __init__(self, *, test: UnitTest) -> None:
        self.test = test

    def on_visit(self, node: cst.CSTNode) -> bool:
        for child in node.children:
            parent = self.get_metadata(ParentNodeProvider, child)
            self.test.assertEqual(parent, node)
        return True


class ParentNodeProviderTest(UnitTest):
    @data_provider(
        (
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
            (
                """
                global_var = None
                @cls_attr
                class Cls(cls_attr, kwarg=cls_attr):
                    cls_attr = 5
                    def f():
                        pass
                """,
            ),
            (
                """
                iterator = None
                condition = None
                [elt for target in iterator if condition]
                {elt for target in iterator if condition}
                {elt: target for target in iterator if condition}
                (elt for target in iterator if condition)
                """,
            ),
        )
    )
    def test_parent_node_provier(self, code: str) -> None:
        wrapper = MetadataWrapper(cst.parse_module(dedent(code)))
        wrapper.visit(DependentVisitor(test=self))
