# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
# pyre-strict

from libcst.codemod import CodemodTest
from libcst.codemod.commands.fix_variadic_callable import FixVariadicCallableCommmand


class TestFixVariadicCallableCommmand(CodemodTest):
    TRANSFORM = FixVariadicCallableCommmand

    def test_callable_typing(self) -> None:
        before = """
            from typing import Callable
            x: Callable[[...], int] = ...
        """
        after = """
            from typing import Callable
            x: Callable[..., int] = ...
        """
        self.assertCodemod(before, after)

    def test_callable_typing_alias(self) -> None:
        before = """
            import typing as t
            x: t.Callable[[...], int] = ...
        """
        after = """
            import typing as t
            x: t.Callable[..., int] = ...
        """
        self.assertCodemod(before, after)

    def test_callable_import_alias(self) -> None:
        before = """
            from typing import Callable as C
            x: C[[...], int] = ...
        """
        after = """
            from typing import Callable as C
            x: C[..., int] = ...
        """
        self.assertCodemod(before, after)

    def test_callable_with_optional(self) -> None:
        before = """
            from typing import Callable
            def foo(bar: Optional[Callable[[...], int]]) -> Callable[[...], int]:
                ...
        """
        after = """
            from typing import Callable
            def foo(bar: Optional[Callable[..., int]]) -> Callable[..., int]:
                ...
        """
        self.assertCodemod(before, after)

    def test_callable_with_arguments(self) -> None:
        before = """
            from typing import Callable
            x: Callable[[int], int]
        """
        after = """
            from typing import Callable
            x: Callable[[int], int]
        """
        self.assertCodemod(before, after)

    def test_callable_with_variadic_arguments(self) -> None:
        before = """
            from typing import Callable
            x: Callable[[int, int, ...], int]
        """
        after = """
            from typing import Callable
            x: Callable[[int, int, ...], int]
        """
        self.assertCodemod(before, after)

    def test_callable_no_arguments(self) -> None:
        before = """
            from typing import Callable
            x: Callable
        """
        after = """
            from typing import Callable
            x: Callable
        """
        self.assertCodemod(before, after)
