# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
import re
from abc import ABC
from typing import Pattern

import libcst
from libcst.codemod import CodemodContext, VisitorBasedCodemodCommand
from libcst.helpers import insert_header_comments


class AddPyreDirectiveCommand(VisitorBasedCodemodCommand, ABC):
    PYRE_TAG: str

    def __init__(self, context: CodemodContext) -> None:
        super().__init__(context)
        self._regex_pattern: Pattern[str] = re.compile(
            rf"^#\s+pyre-{self.PYRE_TAG}\s*$"
        )
        self.needs_add = True

    def visit_Comment(self, node: libcst.Comment) -> None:
        if self._regex_pattern.search(node.value):
            self.needs_add = False

    def leave_Module(
        self, original_node: libcst.Module, updated_node: libcst.Module
    ) -> libcst.Module:
        # If the tag already exists, don't modify the file.
        if not self.needs_add:
            return updated_node

        return insert_header_comments(updated_node, [f"# pyre-{self.PYRE_TAG}"])


class AddPyreStrictCommand(AddPyreDirectiveCommand):
    """
    Given a source file, we'll add the strict tag if the file doesn't already
    contain it.
    """

    PYRE_TAG: str = "strict"

    DESCRIPTION: str = "Add the 'pyre-strict' tag to a module."


class AddPyreUnsafeCommand(AddPyreDirectiveCommand):
    """
    Given a source file, we'll add the unsafe tag if the file doesn't already
    contain it.
    """

    PYRE_TAG: str = "unsafe"

    DESCRIPTION: str = "Add the 'pyre-unsafe' tag to a module."
