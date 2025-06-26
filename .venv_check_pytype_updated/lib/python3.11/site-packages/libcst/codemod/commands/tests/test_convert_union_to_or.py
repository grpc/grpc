# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
# pyre-strict

from libcst.codemod import CodemodTest
from libcst.codemod.commands.convert_union_to_or import ConvertUnionToOrCommand


class TestConvertUnionToOrCommand(CodemodTest):
    TRANSFORM = ConvertUnionToOrCommand

    def test_simple_union(self) -> None:
        before = """
            from typing import Union
            x: Union[int, str]
        """
        after = """
            x: int | str
        """
        self.assertCodemod(before, after)

    def test_nested_union(self) -> None:
        before = """
            from typing import Union
            x: Union[int, Union[str, float]]
        """
        after = """
            x: int | str | float
        """
        self.assertCodemod(before, after)

    def test_single_type_union(self) -> None:
        before = """
            from typing import Union
            x: Union[int]
        """
        after = """
            x: int
        """
        self.assertCodemod(before, after)

    def test_union_with_alias(self) -> None:
        before = """
            import typing as t
            x: t.Union[int, str]
        """
        after = """
            import typing as t
            x: int | str
        """
        self.assertCodemod(before, after)

    def test_union_with_unused_import(self) -> None:
        before = """
            from typing import Union, List
            x: Union[int, str]
        """
        after = """
            from typing import List
            x: int | str
        """
        self.assertCodemod(before, after)

    def test_union_no_import(self) -> None:
        before = """
            x: Union[int, str]
        """
        after = """
            x: Union[int, str]
        """
        self.assertCodemod(before, after)

    def test_union_in_function(self) -> None:
        before = """
            from typing import Union
            def foo(x: Union[int, str]) -> Union[float, None]:
                ...
        """
        after = """
            def foo(x: int | str) -> float | None:
                ...
        """
        self.assertCodemod(before, after)
