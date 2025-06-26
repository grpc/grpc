# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from typing import Set

from libcst import MetadataWrapper, parse_module
from libcst.codemod import CodemodContext, CodemodTest
from libcst.codemod.visitors import GatherUnusedImportsVisitor
from libcst.testing.utils import UnitTest


class TestGatherUnusedImportsVisitor(UnitTest):
    def gather_imports(self, code: str) -> Set[str]:
        mod = MetadataWrapper(parse_module(CodemodTest.make_fixture_data(code)))
        mod.resolve_many(GatherUnusedImportsVisitor.METADATA_DEPENDENCIES)
        instance = GatherUnusedImportsVisitor(CodemodContext(wrapper=mod))
        mod.visit(instance)
        return {
            alias.evaluated_alias or alias.evaluated_name
            for alias, _ in instance.unused_imports
        }

    def test_no_imports(self) -> None:
        imports = self.gather_imports(
            """
            foo = 1
            """
        )
        self.assertEqual(imports, set())

    def test_dotted_imports(self) -> None:
        imports = self.gather_imports(
            """
            import a.b.c, d
            import x.y
            a.b(d)
            """
        )
        self.assertEqual(imports, {"x.y"})

    def test_alias(self) -> None:
        imports = self.gather_imports(
            """
            from bar import baz as baz_alias
            import bar as bar_alias
            bar_alias()
            """
        )
        self.assertEqual(imports, {"baz_alias"})

    def test_import_complex(self) -> None:
        imports = self.gather_imports(
            """
            import bar
            import baz, qux
            import a.b
            import c.d
            import x.y.z
            import e.f as g
            import h.i as j

            def foo() -> None:
                c.d(qux)
                x.u
                j()
            """
        )
        self.assertEqual(imports, {"bar", "baz", "a.b", "g"})

    def test_import_from_complex(self) -> None:
        imports = self.gather_imports(
            """
            from bar import qux, quux
            from a.b import c
            from d.e import f
            from h.i import j as k
            from l.m import n as o
            from x import *

            def foo() -> None:
                f(qux)
                k()
            """
        )
        self.assertEqual(imports, {"quux", "c", "o"})

    def test_exports(self) -> None:
        imports = self.gather_imports(
            """
            import a
            __all__ = ["a"]
            """
        )
        self.assertEqual(imports, set())

    def test_string_annotation(self) -> None:
        imports = self.gather_imports(
            """
            from a import b
            from c import d
            import m, n.blah
            foo: "b[int]"
            bar: List["d"]
            quux: List["m.blah"]
            alma: List["n.blah"]
            """
        )
        self.assertEqual(imports, set())

    def test_typevars(self) -> None:
        imports = self.gather_imports(
            """
            from typing import TypeVar as Sneaky
            from a import b
            t = Sneaky("t", bound="b")
            """
        )
        self.assertEqual(imports, set())

    def test_future(self) -> None:
        imports = self.gather_imports(
            """
            from __future__ import cool_feature
            """
        )
        self.assertEqual(imports, set())
