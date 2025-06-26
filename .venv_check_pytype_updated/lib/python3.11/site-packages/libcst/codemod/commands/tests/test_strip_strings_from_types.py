# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from libcst.codemod import CodemodTest
from libcst.codemod.commands.strip_strings_from_types import StripStringsCommand


class TestStripStringsCodemod(CodemodTest):
    TRANSFORM = StripStringsCommand

    def test_noop(self) -> None:
        before = """
            foo: str = ""

            class Class:
                pass

            def foo(a: Class, **kwargs: str) -> Class:
                t: Class = Class()  # This is a comment
                bar = ""
                return t
        """
        after = """
            foo: str = ""

            class Class:
                pass

            def foo(a: Class, **kwargs: str) -> Class:
                t: Class = Class()  # This is a comment
                bar = ""
                return t
        """

        self.assertCodemod(before, after)

    def test_non_async(self) -> None:
        before = """
            class Class:
                pass

            def foo(a: "Class", **kwargs: "str") -> "Class":
                t: "Class" = Class()  # This is a comment
                return t
        """
        after = """
            from __future__ import annotations

            class Class:
                pass

            def foo(a: Class, **kwargs: str) -> Class:
                t: Class = Class()  # This is a comment
                return t
        """

        self.assertCodemod(before, after)

    def test_async(self) -> None:
        before = """
            class Class:
                pass

            async def foo(a: "Class", **kwargs: "str") -> "Class":
                t: "Class" = Class()  # This is a comment
                return t
        """
        after = """
            from __future__ import annotations

            class Class:
                pass

            async def foo(a: Class, **kwargs: str) -> Class:
                t: Class = Class()  # This is a comment
                return t
        """

        self.assertCodemod(before, after)

    def test_recursive(self) -> None:
        before = """
            class Class:
                pass

            def foo(a: List["Class"]):
                pass

            def bar(a: List[Optional["Class"]]):
                pass

            def baz(a: "List[Class]"):
                pass
        """
        after = """
            from __future__ import annotations

            class Class:
                pass

            def foo(a: List[Class]):
                pass

            def bar(a: List[Optional[Class]]):
                pass

            def baz(a: List[Class]):
                pass
        """

        self.assertCodemod(before, after)

    def test_literal(self) -> None:
        before = """
            from typing_extensions import Literal

            class Class:
                pass

            def foo(a: Literal["one", "two", "three"]):
                pass

            def bar(a: Union["Class", Literal["one", "two", "three"]]):
                pass
        """
        after = """
            from __future__ import annotations
            from typing_extensions import Literal

            class Class:
                pass

            def foo(a: Literal["one", "two", "three"]):
                pass

            def bar(a: Union[Class, Literal["one", "two", "three"]]):
                pass
        """

        self.assertCodemod(before, after)

    def test_literal_alias(self) -> None:
        before = """
            from typing_extensions import Literal as Lit

            class Class:
                pass

            def foo(a: Lit["one", "two", "three"]):
                pass

            def bar(a: Union["Class", Lit["one", "two", "three"]]):
                pass
        """
        after = """
            from __future__ import annotations
            from typing_extensions import Literal as Lit

            class Class:
                pass

            def foo(a: Lit["one", "two", "three"]):
                pass

            def bar(a: Union[Class, Lit["one", "two", "three"]]):
                pass
        """

        self.assertCodemod(before, after)

    def test_literal_object(self) -> None:
        before = """
            import typing_extensions

            class Class:
                pass

            def foo(a: typing_extensions.Literal["one", "two", "three"]):
                pass

            def bar(a: Union["Class", typing_extensions.Literal["one", "two", "three"]]):
                pass
        """
        after = """
            from __future__ import annotations
            import typing_extensions

            class Class:
                pass

            def foo(a: typing_extensions.Literal["one", "two", "three"]):
                pass

            def bar(a: Union[Class, typing_extensions.Literal["one", "two", "three"]]):
                pass
        """

        self.assertCodemod(before, after)

    def test_literal_object_alias(self) -> None:
        before = """
            import typing_extensions as typext

            class Class:
                pass

            def foo(a: typext.Literal["one", "two", "three"]):
                pass

            def bar(a: Union["Class", typext.Literal["one", "two", "three"]]):
                pass
        """
        after = """
            from __future__ import annotations
            import typing_extensions as typext

            class Class:
                pass

            def foo(a: typext.Literal["one", "two", "three"]):
                pass

            def bar(a: Union[Class, typext.Literal["one", "two", "three"]]):
                pass
        """

        self.assertCodemod(before, after)
