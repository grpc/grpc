# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from libcst.codemod import CodemodTest
from libcst.codemod.commands.remove_pyre_directive import (
    RemovePyreStrictCommand,
    RemovePyreUnsafeCommand,
)


class TestRemovePyreStrictCommand(CodemodTest):
    TRANSFORM = RemovePyreStrictCommand

    def test_remove_from_file(self) -> None:
        before = """
            # pyre-strict
            def baz() -> List[Foo]:
                pass
        """
        after = """
            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)

    def test_remove_from_file_without_pyre_strict(self) -> None:
        """
        We shouldn't be removing pyre-strict to a file that doesn't have it.
        """
        before = """
            def baz() -> List[Foo]:
                pass
        """
        after = """
            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)

    def test_remove_from_file_with_pyre_strict_after(self) -> None:
        """
        Test removal if pyre-strict is after comments.
        """
        before = """
            # THIS IS A COMMENT!
            # pyre-strict
            def baz() -> List[Foo]:
                pass
        """
        after = """
            # THIS IS A COMMENT!
            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)

    def test_remove_from_file_with_pyre_strict_before(self) -> None:
        """
        Test removal if pyre-strict is before comments.
        """
        before = """
            # pyre-strict
            # THIS IS A COMMENT!
            def baz() -> List[Foo]:
                pass
        """
        after = """
            # THIS IS A COMMENT!
            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)

    def test_remove_from_file_with_comment(self) -> None:
        """
        We should preserve comments and spacing when removing.
        """
        before = """
            # YO I'M A COMMENT
            # pyre-strict


            def baz() -> List[Foo]:
                pass
        """
        after = """
            # YO I'M A COMMENT


            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)


class TestRemovePyreUnsafeCommand(CodemodTest):
    TRANSFORM = RemovePyreUnsafeCommand

    def test_remove_from_file(self) -> None:
        before = """
            # pyre-unsafe
            def baz() -> List[Foo]:
                pass
        """
        after = """
            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)

    def test_remove_from_file_without_pyre_unsafe(self) -> None:
        """
        We shouldn't be removing pyre-unsafe to a file that doesn't have it.
        """
        before = """
            def baz() -> List[Foo]:
                pass
        """
        after = """
            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)

    def test_remove_from_file_with_pyre_unsafe_after(self) -> None:
        """
        Test removal if pyre-unsafe is after comments.
        """
        before = """
            # THIS IS A COMMENT!
            # pyre-unsafe
            def baz() -> List[Foo]:
                pass
        """
        after = """
            # THIS IS A COMMENT!
            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)

    def test_remove_from_file_with_pyre_unsafe_before(self) -> None:
        """
        Test removal if pyre-unsafe is before comments.
        """
        before = """
            # pyre-unsafe
            # THIS IS A COMMENT!
            def baz() -> List[Foo]:
                pass
        """
        after = """
            # THIS IS A COMMENT!
            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)

    def test_remove_from_file_with_comment(self) -> None:
        """
        We should preserve comments and spacing when removing.
        """
        before = """
            # YO I'M A COMMENT
            # pyre-unsafe


            def baz() -> List[Foo]:
                pass
        """
        after = """
            # YO I'M A COMMENT


            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)
