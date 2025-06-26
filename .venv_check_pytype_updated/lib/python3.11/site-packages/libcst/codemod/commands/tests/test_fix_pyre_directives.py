# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from libcst.codemod import CodemodTest
from libcst.codemod.commands.fix_pyre_directives import FixPyreDirectivesCommand


class TestFixPyreDirectivesCommand(CodemodTest):
    TRANSFORM = FixPyreDirectivesCommand

    def test_no_need_to_fix_simple(self) -> None:
        """
        Tests that a pyre-strict inside the module header doesn't get touched.
        """
        after = (
            before
        ) = """
            # pyre-strict
            from typing import List

            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)

    def test_no_need_to_fix_complex_bottom(self) -> None:
        """
        Tests that a pyre-strict inside the module header doesn't get touched.
        """
        after = (
            before
        ) = """
            # This is some header comment.
            #
            # pyre-strict
            from typing import List

            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)

    def test_no_need_to_fix_complex_top(self) -> None:
        """
        Tests that a pyre-strict inside the module header doesn't get touched.
        """
        after = (
            before
        ) = """
            # pyre-strict
            #
            # This is some header comment.

            from typing import List

            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)

    def test_fix_misspelled_header(self) -> None:
        """
        Tests that we correctly address poor spelling of a comment.
        """
        before = """
            # pyre strict
            from typing import List

            def baz() -> List[Foo]:
                pass
        """
        after = """
            # pyre-strict
            from typing import List

            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)

    def test_fix_misspelled_body(self) -> None:
        """
        Tests that we correctly address poor spelling of a comment.
        """
        before = """
            from typing import List
            # pyre strict

            def baz() -> List[Foo]:
                pass
        """
        after = """
            # pyre-strict
            from typing import List

            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)

    def test_fix_header_duplicate(self) -> None:
        """
        Tests that we correctly remove a duplicate, even with a mistake.
        """
        before = """
            # pyre-strict
            # pyre-strict
            from typing import List

            def baz() -> List[Foo]:
                pass
        """
        after = """
            # pyre-strict
            from typing import List

            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)

    def test_fix_body_duplicate(self) -> None:
        """
        Tests that we correctly remove a duplicate, even with a mistake.
        """
        before = """
            # This is a comment.
            #
            # pyre-strict
            from typing import List

            # pyre-strict
            def baz() -> List[Foo]:
                pass
        """
        after = """
            # This is a comment.
            #
            # pyre-strict
            from typing import List

            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)

    def test_fix_misspelled_header_duplicate(self) -> None:
        """
        Tests that we correctly remove a duplicate, even with a mistake.
        """
        before = """
            # pyre-strict
            # pyre strict
            from typing import List

            def baz() -> List[Foo]:
                pass
        """
        after = """
            # pyre-strict
            from typing import List

            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)

    def test_fix_misspelled_header_duplicate_body(self) -> None:
        """
        Tests that we correctly remove a duplicate, even with a mistake.
        """
        before = """
            # pyre-strict
            from typing import List
            # pyre strict

            def baz() -> List[Foo]:
                pass
        """
        after = """
            # pyre-strict
            from typing import List

            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)

    def test_fix_wrong_location(self) -> None:
        """
        Tests that we correctly move a badly-located pyre-strict.
        """
        before = """
            from typing import List

            # pyre-strict
            def baz() -> List[Foo]:
                pass
        """
        after = """
            # pyre-strict
            from typing import List

            def baz() -> List[Foo]:
                pass
        """
        self.assertCodemod(before, after)
