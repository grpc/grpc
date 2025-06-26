# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from libcst import parse_module
from libcst.codemod import CodemodContext, CodemodTest
from libcst.codemod.visitors import GatherGlobalNamesVisitor
from libcst.testing.utils import UnitTest


class TestGatherGlobalNamesVisitor(UnitTest):
    def gather_global_names(self, code: str) -> GatherGlobalNamesVisitor:
        transform_instance = GatherGlobalNamesVisitor(
            CodemodContext(full_module_name="a.b.foobar")
        )
        input_tree = parse_module(CodemodTest.make_fixture_data(code))
        input_tree.visit(transform_instance)
        return transform_instance

    def test_gather_nothing(self) -> None:
        code = """
            from a import b
            b()
        """
        gatherer = self.gather_global_names(code)
        self.assertEqual(gatherer.global_names, set())
        self.assertEqual(gatherer.class_names, set())
        self.assertEqual(gatherer.function_names, set())

    def test_globals(self) -> None:
        code = """
            x = 1
            y = 2
            def foo(): pass
            class Foo: pass
        """
        gatherer = self.gather_global_names(code)
        self.assertEqual(gatherer.global_names, {"x", "y"})
        self.assertEqual(gatherer.class_names, {"Foo"})
        self.assertEqual(gatherer.function_names, {"foo"})

    def test_omit_nested(self) -> None:
        code = """
            def foo():
                x = 1

            class Foo:
                def method(self): pass
        """
        gatherer = self.gather_global_names(code)
        self.assertEqual(gatherer.global_names, set())
        self.assertEqual(gatherer.class_names, {"Foo"})
        self.assertEqual(gatherer.function_names, {"foo"})
