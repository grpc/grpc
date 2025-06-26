# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from libcst.codemod import CodemodTest
from libcst.codemod.commands.add_pyre_directive import AddPyreUnsafeCommand


class TestAddPyreUnsafeCommand(CodemodTest):
    TRANSFORM = AddPyreUnsafeCommand

    def test_add_to_file(self) -> None:
        before = """
            def baz() -> List[Foo]:
                pass
        """
        after = """
            # pyre-unsafe
            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)

    def test_add_to_file_with_pyre_unsafe(self) -> None:
        """
        We shouldn't be adding pyre-unsafe to a file that already has it.
        """
        before = """
            # pyre-unsafe
            def baz() -> List[Foo]:
                pass
        """
        after = """
            # pyre-unsafe
            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)

    def test_add_to_file_with_pyre_unsafe_after(self) -> None:
        """
        We shouldn't be adding pyre-unsafe to a file that already has it.
        """
        before = """
            # THIS IS A COMMENT!
            # pyre-unsafe
            def baz() -> List[Foo]:
                pass
        """
        after = """
            # THIS IS A COMMENT!
            # pyre-unsafe
            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)

    def test_add_to_file_with_pyre_unsafe_before(self) -> None:
        """
        We shouldn't be adding pyre-unsafe to a file that already has it.
        """
        before = """
            # pyre-unsafe
            # THIS IS A COMMENT!
            def baz() -> List[Foo]:
                pass
        """
        after = """
            # pyre-unsafe
            # THIS IS A COMMENT!
            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)

    def test_empty_file(self) -> None:
        """
        If a file is empty, we should still add it.
        """
        before = ""
        after = "# pyre-unsafe"
        self.assertCodemod(before, after)

    def test_add_to_file_with_comment(self) -> None:
        """
        We should add pyre-unsafe after the last comment at the top of a file.
        """
        before = """
            # YO I'M A COMMENT


            def baz() -> List[Foo]:
                pass
        """
        after = """
            # YO I'M A COMMENT
            # pyre-unsafe


            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)

    def test_add_to_file_with_import(self) -> None:
        """
        Tests that adding to a file with an import works properly.
        """
        before = """
            from typing import List

            def baz() -> List[Foo]:
                pass
        """
        after = """
            # pyre-unsafe
            from typing import List

            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)
