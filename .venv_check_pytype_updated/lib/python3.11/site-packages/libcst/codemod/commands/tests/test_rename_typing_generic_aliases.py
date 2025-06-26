# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
# pyre-strict

from libcst.codemod import CodemodTest
from libcst.codemod.commands.rename_typing_generic_aliases import (
    RenameTypingGenericAliases,
)


class TestRenameCommand(CodemodTest):
    TRANSFORM = RenameTypingGenericAliases

    def test_rename_typing_generic_alias(self) -> None:
        before = """
            from typing import List, Set, Dict, FrozenSet, Tuple
            x: List[int] = []
            y: Set[int] = set()
            z: Dict[str, int] = {}
            a: FrozenSet[str] = frozenset()
            b: Tuple[int, str] = (1, "hello")
        """
        after = """
            x: list[int] = []
            y: set[int] = set()
            z: dict[str, int] = {}
            a: frozenset[str] = frozenset()
            b: tuple[int, str] = (1, "hello")
        """
        self.assertCodemod(before, after)
