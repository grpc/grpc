# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
# pyre-strict

from libcst.codemod import CodemodTest
from libcst.codemod.commands.rename import RenameCommand


class TestRenameCommand(CodemodTest):
    TRANSFORM = RenameCommand

    def test_rename_name(self) -> None:
        before = """
            from foo import bar

            def test() -> None:
                bar(5)
        """
        after = """
            from baz import qux

            def test() -> None:
                qux(5)
        """

        self.assertCodemod(before, after, old_name="foo.bar", new_name="baz.qux")

    def test_rename_to_builtin(self) -> None:
        before = """
            from typing import List
            x: List[int] = []
        """
        after = """
            x: list[int] = []
        """

        self.assertCodemod(
            before, after, old_name="typing.List", new_name="builtins.list"
        )

    def test_rename_name_asname(self) -> None:
        before = """
            from foo import bar as bla

            def test() -> None:
                bla(5)
        """
        after = """
            from baz import qux

            def test() -> None:
                qux(5)
        """

        self.assertCodemod(
            before,
            after,
            old_name="foo.bar",
            new_name="baz.qux",
        )

    def test_rename_repeated_name_with_asname(self) -> None:
        before = """
            from foo import foo as bla

            def test() -> None:
                bla.bla(5)
        """
        after = """
            from baz import qux

            def test() -> None:
                qux.bla(5)
        """
        self.assertCodemod(
            before,
            after,
            old_name="foo.foo",
            new_name="baz.qux",
        )

    def test_rename_attr(self) -> None:
        before = """
            import a.b

            def test() -> None:
                a.b.c(5)
        """
        after = """
            import d.e

            def test() -> None:
                d.e.f(5)
        """

        self.assertCodemod(
            before,
            after,
            old_name="a.b.c",
            new_name="d.e.f",
        )

    def test_rename_attr_asname(self) -> None:
        before = """
            import foo as bar

            def test() -> None:
                bar.qux(5)
        """
        after = """
            import baz

            def test() -> None:
                baz.quux(5)
        """

        self.assertCodemod(
            before,
            after,
            old_name="foo.qux",
            new_name="baz.quux",
        )

    def test_rename_attr_asname_2(self) -> None:
        before = """
            import foo.qux as bar

            def test() -> None:
                bar.z(5)
        """
        after = """
            import baz.quux

            def test() -> None:
                baz.quux.z(5)
        """

        self.assertCodemod(
            before,
            after,
            old_name="foo.qux",
            new_name="baz.quux",
        )

    def test_rename_module_import(self) -> None:
        before = """
            import a.b

            class Foo(a.b.C):
                pass
        """
        after = """
            import c.b

            class Foo(c.b.C):
                pass
        """

        self.assertCodemod(
            before,
            after,
            old_name="a.b",
            new_name="c.b",
        )

    def test_rename_module_import_2(self) -> None:
        before = """
            import a.b

            class Foo(a.b.C):
                pass
        """
        after = """
            import c.b

            class Foo(c.b.C):
                pass
        """

        self.assertCodemod(
            before,
            after,
            old_name="a",
            new_name="c",
        )

    def test_rename_module_import_no_change(self) -> None:
        # Full qualified names don't match, so don't codemod
        before = """
            import a.b

            class Foo(a.b.C):
                pass
        """
        self.assertCodemod(
            before,
            before,
            old_name="b",
            new_name="c.b",
        )

    def test_rename_module_import_from(self) -> None:
        before = """
            from a import b

            class Foo(b.C):
                pass
        """
        after = """
            from c import b

            class Foo(b.C):
                pass
        """

        self.assertCodemod(
            before,
            after,
            old_name="a.b",
            new_name="c.b",
        )

    def test_rename_module_import_from_2(self) -> None:
        before = """
            from a import b

            class Foo(b.C):
                pass
        """
        after = """
            from c import b

            class Foo(b.C):
                pass
        """

        self.assertCodemod(
            before,
            after,
            old_name="a",
            new_name="c",
        )

    def test_rename_class(self) -> None:
        before = """
            from a.b import some_class

            class Foo(some_class):
                pass
        """
        after = """
            from c.b import some_class

            class Foo(some_class):
                pass
        """
        self.assertCodemod(
            before,
            after,
            old_name="a.b.some_class",
            new_name="c.b.some_class",
        )

    def test_rename_importfrom_same_module(self) -> None:
        before = """
            from a.b import Class_1, Class_2

            class Foo(Class_1):
                pass
        """
        after = """
            from a.b import Class_3, Class_2

            class Foo(Class_3):
                pass
        """
        self.assertCodemod(
            before,
            after,
            old_name="a.b.Class_1",
            new_name="a.b.Class_3",
        )

    def test_rename_importfrom_same_module_2(self) -> None:
        before = """
            from a.b import module_1, module_2

            class Foo(module_1.Class_1):
                pass
            class Fooo(module_2.Class_2):
                pass
        """
        after = """
            from a.b import module_2
            from a.b.module_3 import Class_3

            class Foo(Class_3):
                pass
            class Fooo(module_2.Class_2):
                pass
        """
        self.assertCodemod(
            before,
            after,
            old_name="a.b.module_1.Class_1",
            new_name="a.b.module_3.Class_3",
        )

    def test_import_same_module(self) -> None:
        before = """
            import logging
            logging.warn(1)
        """
        after = """
            import logging
            logging.warning(1)
        """
        self.assertCodemod(
            before,
            after,
            old_name="logging.warn",
            new_name="logging.warning",
        )

    def test_import_same_dotted_module(self) -> None:
        before = """
            import a.b
            a.b.warn(1)
        """
        after = """
            import a.b
            a.b.warning(1)
        """
        self.assertCodemod(
            before,
            after,
            old_name="a.b.warn",
            new_name="a.b.warning",
        )

    def test_rename_local_variable(self) -> None:
        before = """
            x = 5
            y = 5 + x
        """
        after = """
            z = 5
            y = 5 + z
        """

        self.assertCodemod(
            before,
            after,
            old_name="x",
            new_name="z",
        )

    def test_module_does_not_change(self) -> None:
        before = """
            from a import b

            class Foo(b):
                pass
        """
        after = """
            from a import c

            class Foo(c):
                pass
        """
        self.assertCodemod(before, after, old_name="a.b", new_name="a.c")

    def test_other_imports_untouched(self) -> None:
        before = """
            import a, b, c

            class Foo(a.z):
                bar: b.bar
                baz: c.baz
        """
        after = """
            import d, b, c

            class Foo(d.z):
                bar: b.bar
                baz: c.baz
        """
        self.assertCodemod(
            before,
            after,
            old_name="a.z",
            new_name="d.z",
        )

    def test_comma_import(self) -> None:
        before = """
            import a, b, c

            class Foo(a.z):
                bar: b.bar
                baz: c.baz
        """
        after = """
            import a, b, d

            class Foo(a.z):
                bar: b.bar
                baz: d.baz
        """
        self.assertCodemod(
            before,
            after,
            old_name="c.baz",
            new_name="d.baz",
        )

    def test_other_import_froms_untouched(self) -> None:
        before = """
            from a import b, c, d

            class Foo(b):
                bar: c.bar
                baz: d.baz
        """
        after = """
            from a import c, d
            from f import b

            class Foo(b):
                bar: c.bar
                baz: d.baz
        """
        self.assertCodemod(
            before,
            after,
            old_name="a.b",
            new_name="f.b",
        )

    def test_comma_import_from(self) -> None:
        before = """
            from a import b, c, d

            class Foo(b):
                bar: c.bar
                baz: d.baz
        """
        after = """
            from a import b, c
            from f import d

            class Foo(b):
                bar: c.bar
                baz: d.baz
        """
        self.assertCodemod(
            before,
            after,
            old_name="a.d",
            new_name="f.d",
        )

    def test_comma_import_from_parens(self) -> None:
        before = """
            from a import (
                b,
                c,
                d,
            )
            from x import (y,)

            class Foo(b):
                bar: c.bar
                baz: d.baz
        """
        after = """
            from a import (
                b,
                c,
                )
            from x import (y,)
            from f import d

            class Foo(b):
                bar: c.bar
                baz: d.baz
        """
        self.assertCodemod(
            before,
            after,
            old_name="a.d",
            new_name="f.d",
        )

    def test_no_removal_of_import_in_use(self) -> None:
        before = """
            import a

            class Foo(a.b):
                pass
            class Foo2(a.c):
                pass
        """
        after = """
            import a, z

            class Foo(z.b):
                pass
            class Foo2(a.c):
                pass
        """
        self.assertCodemod(
            before,
            after,
            old_name="a.b",
            new_name="z.b",
        )

    def test_no_removal_of_dotted_import_in_use(self) -> None:
        before = """
            import a.b

            class Foo(a.b.c):
                pass
            class Foo2(a.b.d):
                pass
        """
        after = """
            import a.b, z.b

            class Foo(z.b.c):
                pass
            class Foo2(a.b.d):
                pass
        """
        self.assertCodemod(
            before,
            after,
            old_name="a.b.c",
            new_name="z.b.c",
        )

    def test_no_removal_of_import_from_in_use(self) -> None:
        before = """
            from a import b

            class Foo(b.some_class):
                bar: b.some_other_class
        """
        after = """
            from a import b
            from blah import some_class

            class Foo(some_class):
                bar: b.some_other_class
        """
        self.assertCodemod(
            before,
            after,
            old_name="a.b.some_class",
            new_name="blah.some_class",
        )

    def test_other_unused_imports_untouched(self) -> None:
        before = """
            import a
            import b

            class Foo(a.obj):
                pass
        """
        after = """
            import c
            import b

            class Foo(c.obj):
                pass
        """
        self.assertCodemod(
            before,
            after,
            old_name="a.obj",
            new_name="c.obj",
        )

    def test_complex_module_rename(self) -> None:
        before = """
            from a.b.c import d

            class Foo(d.e.f):
                pass
        """
        after = """
            from g.h.i import j

            class Foo(j):
                pass
        """
        self.assertCodemod(before, after, old_name="a.b.c.d.e.f", new_name="g.h.i.j")

    def test_complex_module_rename_with_asname(self) -> None:
        before = """
            from a.b.c import d as ddd

            class Foo(ddd.e.f):
                pass
        """
        after = """
            from g.h.i import j

            class Foo(j):
                pass
        """
        self.assertCodemod(before, after, old_name="a.b.c.d.e.f", new_name="g.h.i.j")

    def test_names_with_repeated_substrings(self) -> None:
        before = """
            from aa import aaaa

            class Foo(aaaa.Bar):
                pass
        """
        after = """
            from b import c

            class Foo(c.Bar):
                pass
        """
        self.assertCodemod(
            before,
            after,
            old_name="aa.aaaa",
            new_name="b.c",
        )

    def test_repeated_name(self) -> None:
        before = """
            from foo import foo

            def bar():
                foo(5)
        """
        after = """
            from qux import qux

            def bar():
                qux(5)
        """
        self.assertCodemod(
            before,
            after,
            old_name="foo.foo",
            new_name="qux.qux",
        )

    def test_no_codemod(self) -> None:
        before = """
            from foo import bar

            def baz():
                bar(5)
        """
        self.assertCodemod(
            before,
            before,
            old_name="bar",
            new_name="qux",
        )

    def test_rename_import_prefix(self) -> None:
        before = """
            import a.b.c.d
        """
        after = """
            import x.y.c.d
        """
        self.assertCodemod(
            before,
            after,
            old_name="a.b",
            new_name="x.y",
        )

    def test_rename_import_from_prefix(self) -> None:
        before = """
            from a.b.c.d import foo
        """
        after = """
            from x.y.c.d import foo
        """
        self.assertCodemod(
            before,
            after,
            old_name="a.b",
            new_name="x.y",
        )

    def test_rename_multiple_occurrences(self) -> None:
        before = """
            from a import b

            class Foo(b.some_class):
                pass
            class Foobar(b.some_class):
                pass
        """
        after = """
            from c.d import some_class

            class Foo(some_class):
                pass
            class Foobar(some_class):
                pass
        """
        self.assertCodemod(
            before, after, old_name="a.b.some_class", new_name="c.d.some_class"
        )

    def test_rename_multiple_imports(self) -> None:
        before = """
            import a
            from a import b
            from a.c import d

            class Foo(d):
                pass
            class Fooo(b.some_class):
                pass
            class Foooo(a.some_class):
                pass
        """
        after = """
            import z
            from z import b
            from z.c import d

            class Foo(d):
                pass
            class Fooo(b.some_class):
                pass
            class Foooo(z.some_class):
                pass
        """
        self.assertCodemod(before, after, old_name="a", new_name="z")

    def test_input_with_colon_sep(self) -> None:
        before = """
            from a.b.c import d

            class Foo(d.e.f):
                pass
        """
        after = """
            from g.h import i

            class Foo(i.j):
                pass
        """
        self.assertCodemod(before, after, old_name="a.b.c.d.e.f", new_name="g.h:i.j")

    def test_input_with_colon_sep_at_the_end(self) -> None:
        before = """
            from a.b.c import d

            class Foo(d.e):
                pass
        """
        after = """
            import g.h.i.j

            class Foo(g.h.i.j.e):
                pass
        """
        self.assertCodemod(before, after, old_name="a.b.c.d", new_name="g.h.i.j:")

    def test_input_with_colon_sep_at_the_front(self) -> None:
        # This case should treat it as if no colon separator.
        before = """
            from a.b.c import d

            class Foo(d.e):
                pass
        """
        after = """
            from g.h.i import j

            class Foo(j.e):
                pass
        """
        self.assertCodemod(before, after, old_name="a.b.c.d", new_name=":g.h.i.j")

    def test_no_change_because_no_match_was_found(self) -> None:
        before = """
            from foo import bar
            bar(42)
        """
        self.assertCodemod(before, before, old_name="baz.bar", new_name="qux.bar")

    def test_rename_single_with_colon(self) -> None:
        before = """
            from a.b import qux

            print(qux)
        """
        after = """
            from a import b

            print(b.qux)
        """
        self.assertCodemod(
            before,
            after,
            old_name="a.b.qux",
            new_name="a:b.qux",
        )

    def test_import_parent_module(self) -> None:
        before = """
            import a
            a.b.c(a.b.c.d)
        """
        after = """
            from z import c

            c(c.d)
        """
        self.assertCodemod(before, after, old_name="a.b.c", new_name="z.c")

    def test_import_parent_module_2(self) -> None:
        before = """
            import a.b
            a.b.c.d(a.b.c.d.x)
        """
        after = """
            from z import c
            
            c(c.x)
        """
        self.assertCodemod(before, after, old_name="a.b.c.d", new_name="z.c")

    def test_import_parent_module_3(self) -> None:
        before = """
            import a
            a.b.c(a.b.c.d)
        """
        after = """
            import z.c

            z.c(z.c.d)
        """
        self.assertCodemod(before, after, old_name="a.b.c", new_name="z.c:")

    def test_import_parent_module_asname(self) -> None:
        before = """
            import a.b as alias
            alias.c(alias.c.d)
        """
        after = """
            import z
            z.c(z.c.d)
        """
        self.assertCodemod(before, after, old_name="a.b.c", new_name="z.c")

    def test_push_down_toplevel_names(self) -> None:
        before = """
            import foo
            foo.baz()
        """
        after = """
            import quux.foo
            quux.foo.baz()
        """
        self.assertCodemod(before, after, old_name="foo", new_name="quux.foo")

    def test_push_down_toplevel_names_with_asname(self) -> None:
        before = """
            import foo as bar
            bar.baz()
        """
        after = """
            import quux.foo
            quux.foo.baz()
        """
        self.assertCodemod(before, after, old_name="foo", new_name="quux.foo")
