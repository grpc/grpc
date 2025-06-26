# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from libcst.codemod import CodemodTest
from libcst.codemod.commands.convert_namedtuple_to_dataclass import (
    ConvertNamedTupleToDataclassCommand,
)


class ConvertNamedTupleToDataclassCommandTest(CodemodTest):
    TRANSFORM = ConvertNamedTupleToDataclassCommand

    def test_no_change(self) -> None:
        """
        Should result in no change as there are no children of NamedTuple.
        """

        before = """
            @dataclass(frozen=True)
            class Foo:
                pass
        """
        after = """
            @dataclass(frozen=True)
            class Foo:
                pass
        """
        self.assertCodemod(before, after)

    def test_change(self) -> None:
        """
        Should remove the NamedTuple import along with its use as a base class for Foo.
        Should import dataclasses.dataclass and annotate Foo.
        """

        before = """
            from typing import NamedTuple

            class Foo(NamedTuple):
                pass
        """
        after = """
            from dataclasses import dataclass

            @dataclass(frozen=True)
            class Foo:
                pass
        """
        self.assertCodemod(before, after)

    def test_with_decorator_already(self) -> None:
        """
        Should retain existing decorator.
        """

        before = """
            from typing import NamedTuple

            @other_decorator
            class Foo(NamedTuple):
                pass
        """
        after = """
            from dataclasses import dataclass

            @other_decorator
            @dataclass(frozen=True)
            class Foo:
                pass
        """
        self.assertCodemod(before, after)

    def test_multiple_bases(self) -> None:
        """
        Should retain all existing bases other than NamedTuple.
        """

        before = """
            from typing import NamedTuple

            class Foo(NamedTuple, OtherBase, YetAnotherBase):
                pass
        """
        after = """
            from dataclasses import dataclass

            @dataclass(frozen=True)
            class Foo(OtherBase, YetAnotherBase):
                pass
        """
        self.assertCodemod(before, after)

    def test_nested_classes(self) -> None:
        """
        Should perform expected changes on inner classes.
        """

        before = """
            from typing import NamedTuple

            class OuterClass:
                class InnerClass(NamedTuple):
                    pass
        """
        after = """
            from dataclasses import dataclass

            class OuterClass:
                @dataclass(frozen=True)
                class InnerClass:
                    pass
        """
        self.assertCodemod(before, after)

    def test_aliased_object_import(self) -> None:
        """
        Should detect aliased NamedTuple object import and base.
        """

        before = """
            from typing import NamedTuple as nt

            class Foo(nt):
                pass
        """
        after = """
            from dataclasses import dataclass

            @dataclass(frozen=True)
            class Foo:
                pass
        """
        self.assertCodemod(before, after)

    def test_aliased_module_import(self) -> None:
        """
        Should detect aliased `typing` module import and base.
        """

        before = """
            import typing as typ

            class Foo(typ.NamedTuple):
                pass
        """
        after = """
            from dataclasses import dataclass

            @dataclass(frozen=True)
            class Foo:
                pass
        """
        self.assertCodemod(before, after)

    def test_other_unused_imports_not_removed(self) -> None:
        """
        Should not remove any imports other than NamedTuple, even if they are also unused.
        """

        before = """
            from typing import NamedTuple
            import SomeUnusedImport

            class Foo(NamedTuple):
                pass
        """
        after = """
            import SomeUnusedImport
            from dataclasses import dataclass

            @dataclass(frozen=True)
            class Foo:
                pass
        """
        self.assertCodemod(before, after)
