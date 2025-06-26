# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
import libcst as cst
import libcst.matchers as m
from libcst.codemod import CodemodContext, CodemodTest, VisitorBasedCodemodCommand
from libcst.codemod.visitors import AddImportsVisitor, RemoveImportsVisitor
from libcst.metadata import (
    QualifiedName,
    QualifiedNameProvider,
    QualifiedNameSource,
    ScopeProvider,
)
from libcst.testing.utils import data_provider


class TestRemoveImportsCodemod(CodemodTest):
    TRANSFORM = RemoveImportsVisitor

    def test_noop(self) -> None:
        """
        Should do nothing.
        """

        before = """
            def foo() -> None:
                pass
        """
        after = """
            def foo() -> None:
                pass
        """

        self.assertCodemod(before, after, [])

    def test_remove_import_simple(self) -> None:
        """
        Should remove module as import
        """

        before = """
            import bar
            import baz

            def foo() -> None:
                pass
        """
        after = """
            import bar

            def foo() -> None:
                pass
        """

        self.assertCodemod(before, after, [("baz", None, None)])

    def test_remove_fromimport_simple(self) -> None:
        before = "from a import b, c"
        after = "from a import c"
        self.assertCodemod(before, after, [("a", "b", None)])

    def test_remove_fromimport_keeping_standalone_comment(self) -> None:
        before = """
            from foo import (
                bar,
                # comment
                baz,
            )
            from loooong import (
                bar,
                # comment
                short,
                this_stays
            )
            from third import (
                # comment
                short,
                this_stays_too
            )
            from fourth import (
                a,
                # comment
                b,
                c
            )
        """
        after = """
            from foo import (
                # comment
                baz,
            )
            from loooong import (
                this_stays
            )
            from third import (
                this_stays_too
            )
            from fourth import (
                a,
                c
            )
        """
        self.assertCodemod(
            before,
            after,
            [
                ("foo", "bar", None),
                ("loooong", "short", None),
                ("loooong", "bar", None),
                ("third", "short", None),
                ("fourth", "b", None),
            ],
        )

    def test_remove_fromimport_keeping_inline_comment(self) -> None:
        before = """
            from foo import (  # comment
                bar,
                # comment2
                baz,
            )
            from loooong import (
                bar,
                short,  # comment
                # comment2
                this_stays
            )
            from third import (
                short,  # comment
                this_stays_too  # comment2
            )
        """
        after = """
            from foo import (  # comment
                # comment2
                baz,
            )
            from loooong import (
                # comment2
                this_stays
            )
            from third import (
                this_stays_too  # comment2
            )
        """
        self.assertCodemod(
            before,
            after,
            [
                ("foo", "bar", None),
                ("loooong", "short", None),
                ("loooong", "bar", None),
                ("third", "short", None),
            ],
        )

    def test_remove_import_alias_simple(self) -> None:
        """
        Should remove aliased module as import
        """

        before = """
            import bar
            import baz as qux

            def foo() -> None:
                pass
        """
        after = """
            import bar

            def foo() -> None:
                pass
        """

        self.assertCodemod(before, after, [("baz", None, "qux")])

    def test_dont_remove_import_simple(self) -> None:
        """
        Should not remove module import with reference
        """

        before = """
            import bar
            import baz

            def foo() -> None:
                baz.qux()
        """
        after = """
            import bar
            import baz

            def foo() -> None:
                baz.qux()
        """

        self.assertCodemod(before, after, [("baz", None, None)])

    def test_dont_remove_import_alias_simple(self) -> None:
        """
        Should not remove aliased module import with reference
        """

        before = """
            import bar
            import baz as qux

            def foo() -> None:
                qux.quux()
        """
        after = """
            import bar
            import baz as qux

            def foo() -> None:
                qux.quux()
        """

        self.assertCodemod(before, after, [("baz", None, "qux")])

    def test_dont_remove_import_simple_wrong_alias(self) -> None:
        """
        Should not remove module as import since wrong alias
        """

        before = """
            import bar
            import baz

            def foo() -> None:
                pass
        """
        after = """
            import bar
            import baz

            def foo() -> None:
                pass
        """

        self.assertCodemod(before, after, [("baz", None, "qux")])

    def test_dont_remove_import_wrong_alias_simple(self) -> None:
        """
        Should not remove wrong aliased module as import
        """

        before = """
            import bar
            import baz as qux

            def foo() -> None:
                pass
        """
        after = """
            import bar
            import baz as qux

            def foo() -> None:
                pass
        """

        self.assertCodemod(before, after, [("baz", None, None)])

    def test_remove_importfrom_simple(self) -> None:
        """
        Should remove import from
        """

        before = """
            import bar
            from baz import qux

            def foo() -> None:
                pass
        """
        after = """
            import bar

            def foo() -> None:
                pass
        """

        self.assertCodemod(before, after, [("baz", "qux", None)])

    def test_remove_importfrom_alias_simple(self) -> None:
        """
        Should remove import from with alias
        """

        before = """
            import bar
            from baz import qux as quux

            def foo() -> None:
                pass
        """
        after = """
            import bar

            def foo() -> None:
                pass
        """

        self.assertCodemod(before, after, [("baz", "qux", "quux")])

    def test_dont_remove_importfrom_simple(self) -> None:
        """
        Should not remove import from with reference
        """

        before = """
            import bar
            from baz import qux

            def foo() -> None:
                qux()
        """
        after = """
            import bar
            from baz import qux

            def foo() -> None:
                qux()
        """

        self.assertCodemod(before, after, [("baz", "qux", None)])

    def test_dont_remove_importfrom_alias_simple(self) -> None:
        """
        Should not remove aliased import from with reference
        """

        before = """
            import bar
            from baz import qux as quux

            def foo() -> None:
                quux()
        """
        after = """
            import bar
            from baz import qux as quux

            def foo() -> None:
                quux()
        """

        self.assertCodemod(before, after, [("baz", "qux", "quux")])

    def test_dont_remove_importfrom_simple_wrong_alias(self) -> None:
        """
        Should not remove import from since it is wrong alias
        """

        before = """
            import bar
            from baz import qux as quux

            def foo() -> None:
                pass
        """
        after = """
            import bar
            from baz import qux as quux

            def foo() -> None:
                pass
        """

        self.assertCodemod(before, after, [("baz", "qux", None)])

    def test_dont_remove_importfrom_alias_simple_wrong_alias(self) -> None:
        """
        Should not remove import from with wrong alias
        """

        before = """
            import bar
            from baz import qux

            def foo() -> None:
                pass
        """
        after = """
            import bar
            from baz import qux

            def foo() -> None:
                pass
        """

        self.assertCodemod(before, after, [("baz", "qux", "quux")])

    def test_remove_importfrom_relative(self) -> None:
        """
        Should remove import from which is relative
        """

        before = """
            import bar
            from .c import qux

            def foo() -> None:
                pass
        """
        after = """
            import bar

            def foo() -> None:
                pass
        """

        self.assertCodemod(
            before,
            after,
            [("a.b.c", "qux", None)],
            context_override=CodemodContext(
                full_module_name="a.b.foobar", full_package_name="a.b"
            ),
        )

    def test_dont_remove_inuse_importfrom_relative(self) -> None:
        """
        Should not remove import from which is relative since it is in use.
        """

        before = """
            import bar
            from .c import qux

            def foo() -> None:
                qux()
        """
        after = """
            import bar
            from .c import qux

            def foo() -> None:
                qux()
        """

        self.assertCodemod(
            before,
            after,
            [("a.b.c", "qux", None)],
            context_override=CodemodContext(
                full_module_name="a.b.foobar", full_package_name="a.b"
            ),
        )

    def test_dont_remove_wrong_importfrom_relative(self) -> None:
        """
        Should not remove import from which is relative since it is the wrong module.
        """

        before = """
            import bar
            from .c import qux

            def foo() -> None:
                pass
        """
        after = """
            import bar
            from .c import qux

            def foo() -> None:
                pass
        """

        self.assertCodemod(
            before,
            after,
            [("a.b.d", "qux", None)],
            context_override=CodemodContext(
                full_module_name="a.b.foobar", full_package_name="a.b"
            ),
        )

    def test_remove_import_complex(self) -> None:
        """
        Should remove complex module as import
        """

        before = """
            import bar
            import baz, qux
            import a.b
            import c.d
            import x.y.z
            import e.f as g
            import h.i as j

            def foo() -> None:
                c.d()
                x.u
                j()
        """
        after = """
            import bar
            import qux
            import c.d
            import x.y.z
            import h.i as j

            def foo() -> None:
                c.d()
                x.u
                j()
        """

        self.assertCodemod(
            before,
            after,
            [
                ("baz", None, None),
                ("a.b", None, None),
                ("c.d", None, None),
                ("e.f", None, "g"),
                ("h.i", None, "j"),
                ("x.y.z", None, None),
            ],
        )

    def test_remove_fromimport_complex(self) -> None:
        """
        Should remove complex from import
        """

        before = """
            from bar import qux, quux
            from a.b import c
            from d.e import f
            from h.i import j as k
            from l.m import n as o
            from x import *

            def foo() -> None:
                f()
                k()
        """
        after = """
            from bar import qux
            from d.e import f
            from h.i import j as k
            from x import *

            def foo() -> None:
                f()
                k()
        """

        self.assertCodemod(
            before,
            after,
            [
                ("bar", "quux", None),
                ("a.b", "c", None),
                ("d.e", "f", None),
                ("h.i", "j", "k"),
                ("l.m", "n", "o"),
            ],
        )

    def test_remove_import_multiple_assignments(self) -> None:
        """
        Should not remove import with multiple assignments
        """

        before = """
            from foo import bar
            from qux import bar

            def foo() -> None:
                bar()
        """
        after = """
            from foo import bar
            from qux import bar

            def foo() -> None:
                bar()
        """

        self.assertCodemod(before, after, [("foo", "bar", None)])

    def test_remove_multiple_imports(self) -> None:
        """
        Multiple imports
        """
        before = """
            try:
                import a
            except Exception:
                import a

            a.hello()
        """
        after = """
            try:
                import a
            except Exception:
                import a

            a.hello()
        """
        self.assertCodemod(before, after, [("a", None, None)])

        before = """
            try:
                import a
            except Exception:
                import a
        """
        after = """
            try:
                pass
            except Exception:
                pass
        """
        self.assertCodemod(before, after, [("a", None, None)])

    @data_provider(
        (
            # Simple removal, no other uses.
            (
                """
                    from foo import bar
                    from qux import baz

                    def fun() -> None:
                        bar()
                        baz()
                """,
                """
                    from qux import baz

                    def fun() -> None:
                        baz()
                """,
            ),
            # Remove a node, other uses, don't remove import.
            (
                """
                    from foo import bar
                    from qux import baz

                    def fun() -> None:
                        bar()
                        baz()

                    def foobar() -> None:
                        a = bar
                        a()
                """,
                """
                    from foo import bar
                    from qux import baz

                    def fun() -> None:
                        baz()

                    def foobar() -> None:
                        a = bar
                        a()
                """,
            ),
            # Remove an alias.
            (
                """
                    from foo import bar as other
                    from qux import baz

                    def fun() -> None:
                        other()
                        baz()
                """,
                """
                    from qux import baz

                    def fun() -> None:
                        baz()
                """,
            ),
            # Simple removal, no other uses.
            (
                """
                    import foo
                    from qux import baz

                    def fun() -> None:
                        foo.bar()
                        baz()
                """,
                """
                    from qux import baz

                    def fun() -> None:
                        baz()
                """,
            ),
            # Remove a node, other uses, don't remove import.
            (
                """
                    import foo
                    from qux import baz

                    def fun() -> None:
                        foo.bar()
                        baz()

                    def foobar() -> None:
                        a = foo.bar
                        a()
                """,
                """
                    import foo
                    from qux import baz

                    def fun() -> None:
                        baz()

                    def foobar() -> None:
                        a = foo.bar
                        a()
                """,
            ),
            # Remove an alias.
            (
                """
                    import foo as other
                    from qux import baz

                    def fun() -> None:
                        other.bar()
                        baz()
                """,
                """
                    from qux import baz

                    def fun() -> None:
                        baz()
                """,
            ),
        )
    )
    def test_remove_import_by_node_simple(self, before: str, after: str) -> None:
        """
        Given a node that's directly referenced in an import,
        make sure that the import is removed when the node
        is also removed.
        """

        class RemoveBarTransformer(VisitorBasedCodemodCommand):
            METADATA_DEPENDENCIES = (QualifiedNameProvider, ScopeProvider)

            @m.leave(
                m.SimpleStatementLine(
                    body=[
                        m.Expr(
                            m.Call(
                                metadata=m.MatchMetadata(
                                    QualifiedNameProvider,
                                    {
                                        QualifiedName(
                                            source=QualifiedNameSource.IMPORT,
                                            name="foo.bar",
                                        )
                                    },
                                )
                            )
                        )
                    ]
                )
            )
            def _leave_foo_bar(
                self,
                original_node: cst.SimpleStatementLine,
                updated_node: cst.SimpleStatementLine,
            ) -> cst.RemovalSentinel:
                RemoveImportsVisitor.remove_unused_import_by_node(
                    self.context, original_node
                )
                return cst.RemoveFromParent()

        module = cst.parse_module(self.make_fixture_data(before))
        self.assertCodeEqual(
            after, RemoveBarTransformer(CodemodContext()).transform_module(module).code
        )

    def test_remove_import_from_node(self) -> None:
        """
        Make sure that if an import node itself is requested for
        removal, we still do the right thing and only remove it
        if it is unused.
        """

        before = """
            from foo import bar
            from qux import baz
            from foo import qux as other
            from qux import foobar as other2

            def fun() -> None:
                baz()
                other2()
        """
        after = """
            from qux import baz
            from qux import foobar as other2

            def fun() -> None:
                baz()
                other2()
        """

        class RemoveImportTransformer(VisitorBasedCodemodCommand):
            METADATA_DEPENDENCIES = (QualifiedNameProvider, ScopeProvider)

            def visit_ImportFrom(self, node: cst.ImportFrom) -> None:
                RemoveImportsVisitor.remove_unused_import_by_node(self.context, node)

        module = cst.parse_module(self.make_fixture_data(before))
        self.assertCodeEqual(
            after,
            RemoveImportTransformer(CodemodContext()).transform_module(module).code,
        )

    def test_remove_import_node(self) -> None:
        """
        Make sure that if an import node itself is requested for
        removal, we still do the right thing and only remove it
        if it is unused.
        """

        before = """
            import foo
            import qux
            import bar as other
            import foobar as other2

            def fun() -> None:
                qux.baz()
                other2.baz()
        """
        after = """
            import qux
            import foobar as other2

            def fun() -> None:
                qux.baz()
                other2.baz()
        """

        class RemoveImportTransformer(VisitorBasedCodemodCommand):
            METADATA_DEPENDENCIES = (QualifiedNameProvider, ScopeProvider)

            def visit_Import(self, node: cst.Import) -> None:
                RemoveImportsVisitor.remove_unused_import_by_node(self.context, node)

        module = cst.parse_module(self.make_fixture_data(before))
        self.assertCodeEqual(
            after,
            RemoveImportTransformer(CodemodContext()).transform_module(module).code,
        )

    def test_remove_import_with_all(self) -> None:
        """
        Make sure that if an import node itself is requested for
        removal, we don't remove it if it shows up in an __all__
        node.
        """

        before = """
            from foo import bar
            from qux import baz

            __all__ = ["baz"]
        """
        after = """
            from qux import baz

            __all__ = ["baz"]
        """

        class RemoveImportTransformer(VisitorBasedCodemodCommand):
            METADATA_DEPENDENCIES = (QualifiedNameProvider, ScopeProvider)

            def visit_ImportFrom(self, node: cst.ImportFrom) -> None:
                RemoveImportsVisitor.remove_unused_import_by_node(self.context, node)

        module = cst.parse_module(self.make_fixture_data(before))
        self.assertCodeEqual(
            after,
            RemoveImportTransformer(CodemodContext()).transform_module(module).code,
        )

    def test_remove_import_alias_after_inserting(self) -> None:
        before = "from foo import bar, baz"
        after = "from foo import quux, baz"

        class AddRemoveTransformer(VisitorBasedCodemodCommand):
            def visit_Module(self, node: cst.Module) -> None:
                AddImportsVisitor.add_needed_import(self.context, "foo", "quux")
                RemoveImportsVisitor.remove_unused_import(self.context, "foo", "bar")

        module = cst.parse_module(self.make_fixture_data(before))
        self.assertCodeEqual(
            AddRemoveTransformer(CodemodContext()).transform_module(module).code,
            after,
        )

    def test_remove_comma(self) -> None:
        """
        Trailing commas should be removed if and only if the last alias is removed.
        """
        before = """
            from m import (a, b,)
            import x, y
        """
        after = """
            from m import (b,)
            import x
        """
        self.assertCodemod(before, after, [("m", "a", None), ("y", None, None)])
