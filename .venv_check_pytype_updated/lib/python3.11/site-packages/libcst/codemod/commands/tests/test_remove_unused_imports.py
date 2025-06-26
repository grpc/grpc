# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#

from libcst.codemod import CodemodTest
from libcst.codemod.commands.remove_unused_imports import RemoveUnusedImportsCommand


class RemoveUnusedImportsCommandTest(CodemodTest):
    TRANSFORM = RemoveUnusedImportsCommand

    def test_simple_case(self) -> None:
        before = "import a, b\na()"
        after = "import a\na()"
        self.assertCodemod(before, after)

    def test_double_import(self) -> None:
        before = "import a\nimport a\na()"
        self.assertCodemod(before, before)

    def test_conditional_import(self) -> None:
        before = """
            if True:
                import a
            else:
                import b as a
            a()
        """
        self.assertCodemod(before, before)

    def test_unused_in_conditional(self) -> None:
        before = """
            if False:
                import a
        """
        after = """
            if False:
                pass
        """
        self.assertCodemod(before, after)

    def test_type_annotations(self) -> None:
        before = """
            import a
            x: a = 1
        """
        self.assertCodemod(before, before)

    def test_dotted_imports(self) -> None:
        before = """
            import a.b, a.b.c
            import e.f
            import g.h
            import x.y, x.y.z

            def foo() -> None:
                a.b
                e.g
                g.h.i
                x.y.z
        """

        after = """
            import a.b, a.b.c
            import e.f
            import g.h
            import x.y.z

            def foo() -> None:
                a.b
                e.g
                g.h.i
                x.y.z
        """

        self.assertCodemod(before, after)

    def test_enclosed_attributes(self) -> None:
        before = """
            from a.b import c
            import x

            def foo() -> None:
                x.y(c.d()).z()
        """
        self.assertCodemod(before, before)

    def test_access_in_assignment(self) -> None:
        before = """
            from a import b
            b(0)[x] = False
        """
        self.assertCodemod(before, before)

    def test_no_formatting_if_no_unused_imports(self) -> None:
        before = """
            from m import (a, b,)
            a(b, 'look at these ugly quotes')
        """
        self.assertCodemod(before, before)

    def test_suppression_on_first_line_of_multiline_import_refers_to_whole_block(
        self,
    ) -> None:
        before = """
            from a import (  # lint-ignore: unused-import
                b,
                c,
            )
        """
        self.assertCodemod(before, before)

    def test_suppression(self) -> None:
        before = """
            # noqa
            import a, b
            import c
            from x import (
                y,
                z,  # noqa
            )
        """
        after = """
            # noqa
            import a, b
            from x import (
                z,  # noqa
            )
        """
        self.assertCodemod(before, after)
