# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from libcst import parse_module
from libcst.codemod import CodemodContext, CodemodTest
from libcst.codemod.visitors import GatherImportsVisitor
from libcst.testing.utils import UnitTest


class TestGatherImportsVisitor(UnitTest):
    def gather_imports(self, code: str) -> GatherImportsVisitor:
        transform_instance = GatherImportsVisitor(
            CodemodContext(full_module_name="a.b.foobar", full_package_name="a.b")
        )
        input_tree = parse_module(CodemodTest.make_fixture_data(code))
        input_tree.visit(transform_instance)
        return transform_instance

    def test_gather_nothing(self) -> None:
        code = """
            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        gatherer = self.gather_imports(code)
        self.assertEqual(gatherer.module_imports, set())
        self.assertEqual(gatherer.object_mapping, {})
        self.assertEqual(gatherer.module_aliases, {})
        self.assertEqual(gatherer.alias_mapping, {})
        self.assertEqual(len(gatherer.all_imports), 0)

    def test_gather_module(self) -> None:
        code = """
            import a.b.c
            import d

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        gatherer = self.gather_imports(code)
        self.assertEqual(gatherer.module_imports, {"a.b.c", "d"})
        self.assertEqual(gatherer.object_mapping, {})
        self.assertEqual(gatherer.module_aliases, {})
        self.assertEqual(gatherer.alias_mapping, {})
        self.assertEqual(len(gatherer.all_imports), 2)

    def test_gather_aliased_module(self) -> None:
        code = """
            import a.b.c as e
            import d as f

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        gatherer = self.gather_imports(code)
        self.assertEqual(gatherer.module_imports, set())
        self.assertEqual(gatherer.object_mapping, {})
        self.assertEqual(gatherer.module_aliases, {"a.b.c": "e", "d": "f"})
        self.assertEqual(gatherer.alias_mapping, {})
        self.assertEqual(len(gatherer.all_imports), 2)

    def test_gather_object(self) -> None:
        code = """
            from a.b.c import d, e, f

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        gatherer = self.gather_imports(code)
        self.assertEqual(gatherer.module_imports, set())
        self.assertEqual(gatherer.object_mapping, {"a.b.c": {"d", "e", "f"}})
        self.assertEqual(gatherer.module_aliases, {})
        self.assertEqual(gatherer.alias_mapping, {})
        self.assertEqual(len(gatherer.all_imports), 1)

    def test_gather_object_disjoint(self) -> None:
        code = """
            from a.b.c import d, e
            from a.b.c import f

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        gatherer = self.gather_imports(code)
        self.assertEqual(gatherer.module_imports, set())
        self.assertEqual(gatherer.object_mapping, {"a.b.c": {"d", "e", "f"}})
        self.assertEqual(gatherer.module_aliases, {})
        self.assertEqual(gatherer.alias_mapping, {})
        self.assertEqual(len(gatherer.all_imports), 2)

    def test_gather_aliased_object(self) -> None:
        code = """
            from a.b.c import d as e, f as g

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        gatherer = self.gather_imports(code)
        self.assertEqual(gatherer.module_imports, set())
        self.assertEqual(gatherer.object_mapping, {})
        self.assertEqual(gatherer.module_aliases, {})
        self.assertEqual(gatherer.alias_mapping, {"a.b.c": [("d", "e"), ("f", "g")]})
        self.assertEqual(len(gatherer.all_imports), 1)

    def test_gather_aliased_object_disjoint(self) -> None:
        code = """
            from a.b.c import d as e
            from a.b.c import f as g

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        gatherer = self.gather_imports(code)
        self.assertEqual(gatherer.module_imports, set())
        self.assertEqual(gatherer.object_mapping, {})
        self.assertEqual(gatherer.module_aliases, {})
        self.assertEqual(gatherer.alias_mapping, {"a.b.c": [("d", "e"), ("f", "g")]})
        self.assertEqual(len(gatherer.all_imports), 2)

    def test_gather_aliased_object_mixed(self) -> None:
        code = """
            from a.b.c import d as e, f, g

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        gatherer = self.gather_imports(code)
        self.assertEqual(gatherer.module_imports, set())
        self.assertEqual(gatherer.object_mapping, {"a.b.c": {"f", "g"}})
        self.assertEqual(gatherer.module_aliases, {})
        self.assertEqual(gatherer.alias_mapping, {"a.b.c": [("d", "e")]})
        self.assertEqual(len(gatherer.all_imports), 1)

    def test_gather_relative_object(self) -> None:
        code = """
            from .c import d as e, f, g
            from a.b.c import h, i, j

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        gatherer = self.gather_imports(code)
        self.assertEqual(gatherer.module_imports, set())
        self.assertEqual(gatherer.object_mapping, {"a.b.c": {"f", "g", "h", "i", "j"}})
        self.assertEqual(gatherer.module_aliases, {})
        self.assertEqual(gatherer.alias_mapping, {"a.b.c": [("d", "e")]})
        self.assertEqual(len(gatherer.all_imports), 2)
