# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from libcst.codemod import CodemodContext, CodemodTest
from libcst.codemod.visitors import AddImportsVisitor, ImportItem


class TestAddImportsCodemod(CodemodTest):
    TRANSFORM = AddImportsVisitor

    def test_noop(self) -> None:
        """
        Should do nothing.
        """

        before = """
            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(before, after, [])

    def test_add_module_simple(self) -> None:
        """
        Should add module as an import.
        """

        before = """
            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            import a.b.c

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(before, after, [ImportItem("a.b.c", None, None)])

    def test_dont_add_module_simple(self) -> None:
        """
        Should not add module as an import since it exists
        """

        before = """
            import a.b.c

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            import a.b.c

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(before, after, [ImportItem("a.b.c", None, None)])

    def test_add_module_alias_simple(self) -> None:
        """
        Should add module with alias as an import.
        """

        before = """
            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            import a.b.c as d

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(before, after, [ImportItem("a.b.c", None, "d")])

    def test_dont_add_module_alias_simple(self) -> None:
        """
        Should not add module with alias as an import since it exists
        """

        before = """
            import a.b.c as d

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            import a.b.c as d

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(before, after, [ImportItem("a.b.c", None, "d")])

    def test_add_module_complex(self) -> None:
        """
        Should add some modules as an import.
        """

        before = """
            import argparse
            import sys

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            import argparse
            import sys
            import a.b.c
            import defg.hi
            import jkl as h
            import i.j as k

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(
            before,
            after,
            [
                ImportItem("a.b.c", None, None),
                ImportItem("defg.hi", None, None),
                ImportItem("argparse", None, None),
                ImportItem("jkl", None, "h"),
                ImportItem("i.j", None, "k"),
            ],
        )

    def test_add_object_simple(self) -> None:
        """
        Should add object as an import.
        """

        before = """
            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            from a.b.c import D

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(before, after, [ImportItem("a.b.c", "D", None)])

    def test_add_object_alias_simple(self) -> None:
        """
        Should add object with alias as an import.
        """

        before = """
            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            from a.b.c import D as E

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(before, after, [ImportItem("a.b.c", "D", "E")])

    def test_add_future(self) -> None:
        """
        Should add future import before any other imports.
        """

        before = """
            import unittest
            import abc

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            from __future__ import dummy_feature
            import unittest
            import abc

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(
            before, after, [ImportItem("__future__", "dummy_feature", None)]
        )

    def test_dont_add_object_simple(self) -> None:
        """
        Should not add object as an import since it exists.
        """

        before = """
            from a.b.c import D

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            from a.b.c import D

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(before, after, [ImportItem("a.b.c", "D", None)])

    def test_dont_add_object_alias_simple(self) -> None:
        """
        Should not add object as an import since it exists.
        """

        before = """
            from a.b.c import D as E

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            from a.b.c import D as E

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(before, after, [ImportItem("a.b.c", "D", "E")])

    def test_add_object_modify_simple(self) -> None:
        """
        Should modify existing import to add new object
        """

        before = """
            from a.b.c import E, F

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            from a.b.c import D, E, F

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(before, after, [ImportItem("a.b.c", "D", None)])

    def test_add_object_alias_modify_simple(self) -> None:
        """
        Should modify existing import with alias to add new object
        """

        before = """
            from a.b.c import E, F

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            from a.b.c import D as _, E, F

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(before, after, [ImportItem("a.b.c", "D", "_")])

    def test_add_object_modify_complex(self) -> None:
        """
        Should modify existing import to add new object
        """

        before = """
            from a.b.c import E, F, G as H
            from d.e.f import Foo, Bar

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            from a.b.c import D, E, F, G as H
            from d.e.f import Baz as Qux, Foo, Bar
            from g.h.i import V as W, X, Y, Z

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(
            before,
            after,
            [
                ImportItem("a.b.c", "D", None),
                ImportItem("a.b.c", "F", None),
                ImportItem("a.b.c", "G", "H"),
                ImportItem("d.e.f", "Foo", None),
                ImportItem("g.h.i", "Z", None),
                ImportItem("g.h.i", "X", None),
                ImportItem("d.e.f", "Bar", None),
                ImportItem("d.e.f", "Baz", "Qux"),
                ImportItem("g.h.i", "Y", None),
                ImportItem("g.h.i", "V", "W"),
                ImportItem("a.b.c", "F", None),
            ],
        )

    def test_add_and_modify_complex(self) -> None:
        """
        Should correctly add both module and object imports
        """

        before = """
            import argparse
            import sys
            from a.b.c import E, F
            from d.e.f import Foo, Bar
            import bar as baz

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            import argparse
            import sys
            from a.b.c import D, E, F
            from d.e.f import Foo, Bar
            import bar as baz
            import foo
            import qux as quux
            from g.h.i import X, Y, Z

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(
            before,
            after,
            [
                ImportItem("a.b.c", "D", None),
                ImportItem("a.b.c", "F", None),
                ImportItem("d.e.f", "Foo", None),
                ImportItem("sys", None, None),
                ImportItem("g.h.i", "Z", None),
                ImportItem("g.h.i", "X", None),
                ImportItem("d.e.f", "Bar", None),
                ImportItem("g.h.i", "Y", None),
                ImportItem("foo", None, None),
                ImportItem("a.b.c", "F", None),
                ImportItem("bar", None, "baz"),
                ImportItem("qux", None, "quux"),
            ],
        )

    def test_add_import_preserve_doctring_simple(self) -> None:
        """
        Should preserve any doctring if adding to the beginning.
        """

        before = """
            # This is some docstring

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            # This is some docstring

            from a.b.c import D

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(before, after, [ImportItem("a.b.c", "D", None)])

    def test_add_import_preserve_doctring_multiples(self) -> None:
        """
        Should preserve any doctring if adding to the beginning.
        """

        before = """
            # This is some docstring

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            # This is some docstring

            import argparse
            from a.b.c import D

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(
            before,
            after,
            [ImportItem("a.b.c", "D", None), ImportItem("argparse", None, None)],
        )

    def test_strict_module_no_imports(self) -> None:
        """
        First added import in strict module should go after __strict__ flag.
        """
        before = """
            __strict__ = True

            class Foo:
                pass
        """
        after = """
            __strict__ = True
            import argparse

            class Foo:
                pass
        """

        self.assertCodemod(before, after, [ImportItem("argparse", None, None)])

    def test_strict_module_with_imports(self) -> None:
        """
        First added import in strict module should go after __strict__ flag.
        """
        before = """
            __strict__ = True

            import unittest

            class Foo:
                pass
        """
        after = """
            __strict__ = True

            import unittest
            import argparse

            class Foo:
                pass
        """

        self.assertCodemod(before, after, [ImportItem("argparse", None, None)])

    def test_dont_add_relative_object_simple(self) -> None:
        """
        Should not add object as an import since it exists.
        """

        before = """
            from .c import D

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            from .c import D

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(
            before,
            after,
            [ImportItem("a.b.c", "D", None)],
            context_override=CodemodContext(
                full_module_name="a.b.foobar", full_package_name="a.b"
            ),
        )

    def test_add_object_relative_modify_simple(self) -> None:
        """
        Should modify existing import to add new object
        """

        before = """
            from .c import E, F

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            from .c import D, E, F

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(
            before,
            after,
            [ImportItem("a.b.c", "D", None)],
            context_override=CodemodContext(
                full_module_name="a.b.foobar", full_package_name="a.b"
            ),
        )

    def test_import_order(self) -> None:
        """
        The imports should be in alphabetic order of added imports, added import alias, original imports.
        """
        before = """
            from a import b, e, h
        """
        after = """
            from a import c, f, d as x, g as y, b, e, h
        """

        self.assertCodemod(
            before,
            after,
            [
                ImportItem("a", "f", None),
                ImportItem("a", "g", "y"),
                ImportItem("a", "c", None),
                ImportItem("a", "d", "x"),
            ],
            context_override=CodemodContext(
                full_module_name="a.b.foobar", full_package_name="a.b"
            ),
        )

    def test_add_explicit_relative(self) -> None:
        """
        Should add a relative import from .. .
        """

        before = """
            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            from .. import a

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(
            before,
            after,
            [ImportItem("a", None, None, 2)],
        )

    def test_add_explicit_relative_alias(self) -> None:
        """
        Should add a relative import from .. .
        """

        before = """
            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            from .. import a as foo

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(
            before,
            after,
            [ImportItem("a", None, "foo", 2)],
        )

    def test_add_explicit_relative_object_simple(self) -> None:
        """
        Should add a relative import.
        """

        before = """
            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            from ..a import B

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(
            before,
            after,
            [ImportItem("a", "B", None, 2)],
        )

    def test_dont_add_explicit_relative_object_simple(self) -> None:
        """
        Should not add object as an import since it exists.
        """

        before = """
            from ..c import D

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            from ..c import D

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(
            before,
            after,
            [ImportItem("c", "D", None, 2)],
            context_override=CodemodContext(
                full_module_name="a.b.foobar", full_package_name="a.b"
            ),
        )

    def test_add_object_explicit_relative_modify_simple(self) -> None:
        """
        Should modify existing import to add new object.
        """

        before = """
            from ..c import E, F

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            from ..c import D, E, F

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(
            before,
            after,
            [ImportItem("c", "D", None, 2)],
            context_override=CodemodContext(
                full_module_name="a.b.foobar", full_package_name="a.b"
            ),
        )

    def test_add_object_resolve_explicit_relative_modify_simple(self) -> None:
        """
        Should merge a relative new module with an absolute existing one.
        """

        before = """
            from ..c import E, F

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            from ..c import D, E, F

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(
            before,
            after,
            [ImportItem("c", "D", None, 2)],
            context_override=CodemodContext(
                full_module_name="a.b.foobar", full_package_name="a.b"
            ),
        )

    def test_add_object_resolve_dotted_relative_modify_simple(self) -> None:
        """
        Should merge a relative new module with an absolute existing one.
        """

        before = """
            from ..c import E, F

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            from ..c import D, E, F

            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        self.assertCodemod(
            before,
            after,
            [ImportItem("..c", "D", None)],
            context_override=CodemodContext(
                full_module_name="a.b.foobar", full_package_name="a.b"
            ),
        )

    def test_import_in_docstring_module(self) -> None:
        """
        The import should be added after module docstring.
        """
        before = """
            '''Docstring.'''
            import typing
        """
        after = """
            '''Docstring.'''
            from __future__ import annotations
            import typing
        """

        self.assertCodemod(
            before,
            after,
            [ImportItem("__future__", "annotations", None)],
            context_override=CodemodContext(
                full_module_name="a.b.foobar", full_package_name="a.b"
            ),
        )

    def test_import_in_module_with_standalone_string_not_a_docstring(
        self,
    ) -> None:
        """
        The import should be added after the __future__ imports.
        """
        before = """
            from __future__ import annotations
            from __future__ import division

            '''docstring.'''
            def func():
                pass
        """
        after = """
            from __future__ import annotations
            from __future__ import division
            import typing

            '''docstring.'''
            def func():
                pass
        """

        self.assertCodemod(
            before,
            after,
            [ImportItem("typing", None, None)],
            context_override=CodemodContext(
                full_module_name="a.b.foobar", full_package_name="a.b"
            ),
        )

    def test_add_at_first_block(self) -> None:
        """
        Should add the import only at the end of the first import block.
        """

        before = """
            import a
            import b

            e()

            import c
            import d
        """

        after = """
            import a
            import b
            import e

            e()

            import c
            import d
        """

        self.assertCodemod(before, after, [ImportItem("e", None, None)])

    def test_add_no_import_block_before_statement(self) -> None:
        """
        Should add the import before the call.
        """

        before = """
            '''docstring'''
            e()
            import a
            import b
        """

        after = """
            '''docstring'''
            import c

            e()
            import a
            import b
        """

        self.assertCodemod(before, after, [ImportItem("c", None, None)])

    def test_do_not_add_existing(self) -> None:
        """
        Should not add the new object import at existing import since it's not at the top
        """

        before = """
            '''docstring'''
            e()
            import a
            import b
            from c import f
        """

        after = """
            '''docstring'''
            from c import e

            e()
            import a
            import b
            from c import f
        """

        self.assertCodemod(before, after, [ImportItem("c", "e", None)])

    def test_add_existing_at_top(self) -> None:
        """
        Should add new import at exisitng from import at top
        """

        before = """
            '''docstring'''
            from c import d
            e()
            import a
            import b
            from c import f
        """

        after = """
            '''docstring'''
            from c import e, x, d
            e()
            import a
            import b
            from c import f
        """

        self.assertCodemod(
            before, after, [ImportItem("c", "x", None), ImportItem("c", "e", None)]
        )
