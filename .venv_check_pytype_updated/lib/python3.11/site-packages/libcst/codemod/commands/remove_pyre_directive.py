# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
import re
from abc import ABC
from typing import Pattern, Union

import libcst
from libcst.codemod import CodemodContext, VisitorBasedCodemodCommand


class RemovePyreDirectiveCommand(VisitorBasedCodemodCommand, ABC):
    PYRE_TAG: str

    def __init__(self, context: CodemodContext) -> None:
        super().__init__(context)
        self._regex_pattern: Pattern[str] = re.compile(
            rf"^#\s+pyre-{self.PYRE_TAG}\s*$"
        )

    def leave_EmptyLine(
        self, original_node: libcst.EmptyLine, updated_node: libcst.EmptyLine
    ) -> Union[libcst.EmptyLine, libcst.RemovalSentinel]:
        if updated_node.comment is None or not bool(
            self._regex_pattern.search(
                libcst.ensure_type(updated_node.comment, libcst.Comment).value
            )
        ):
            # This is a normal comment
            return updated_node
        # This is a directive comment matching our tag, so remove it.
        return libcst.RemoveFromParent()


class RemovePyreStrictCommand(RemovePyreDirectiveCommand):
    """
    Given a source file, we'll remove the any strict tag if the file already
    contains it.
    """

    DESCRIPTION: str = "Removes the 'pyre-strict' tag from a module."

    PYRE_TAG: str = "strict"


class RemovePyreUnsafeCommand(RemovePyreDirectiveCommand):
    """
    Given a source file, we'll remove the any unsafe tag if the file already
    contains it.
    """

    DESCRIPTION: str = "Removes the 'pyre-unsafe' tag from a module."

    PYRE_TAG: str = "unsafe"
