# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from typing import Dict, Sequence, Union

import libcst
import libcst.matchers as m
from libcst import CSTLogicError
from libcst.codemod import CodemodContext, VisitorBasedCodemodCommand
from libcst.helpers import insert_header_comments


class FixPyreDirectivesCommand(VisitorBasedCodemodCommand):
    """
    Given a source file, we'll move the any strict or unsafe tag to the top of the
    file if it contains one. Also tries to fix typo'd directives.
    """

    DESCRIPTION: str = "Fixes common misspelling and location errors with pyre tags."

    PYRE_TAGS: Sequence[str] = ["strict", "unsafe"]

    def __init__(self, context: CodemodContext) -> None:
        super().__init__(context)
        self.move_strict: Dict[str, bool] = {tag: False for tag in self.PYRE_TAGS}
        self.module_header_tags: Dict[str, int] = {tag: 0 for tag in self.PYRE_TAGS}
        self.in_module_header: bool = False

    def visit_Module_header(self, node: libcst.Module) -> None:
        if self.in_module_header:
            raise CSTLogicError("Logic error!")
        self.in_module_header = True

    def leave_Module_header(self, node: libcst.Module) -> None:
        if not self.in_module_header:
            raise CSTLogicError("Logic error!")
        self.in_module_header = False

    def leave_EmptyLine(
        self, original_node: libcst.EmptyLine, updated_node: libcst.EmptyLine
    ) -> Union[libcst.EmptyLine, libcst.RemovalSentinel]:
        # First, find misplaced lines.
        for tag in self.PYRE_TAGS:
            if m.matches(updated_node, m.EmptyLine(comment=m.Comment(f"# pyre-{tag}"))):
                if self.in_module_header:
                    # We only want to remove this if we've already found another
                    # pyre-strict in the header (that means its duplicated). We
                    # also don't want to move the pyre-strict since its already in
                    # the header, so don't mark that we need to move.
                    self.module_header_tags[tag] += 1
                    if self.module_header_tags[tag] > 1:
                        return libcst.RemoveFromParent()
                    else:
                        return updated_node
                else:
                    # This showed up outside the module header, so move it inside
                    if self.module_header_tags[tag] < 1:
                        self.move_strict[tag] = True
                    return libcst.RemoveFromParent()
            # Now, find misnamed lines
            if m.matches(updated_node, m.EmptyLine(comment=m.Comment(f"# pyre {tag}"))):
                if self.in_module_header:
                    # We only want to remove this if we've already found another
                    # pyre-strict in the header (that means its duplicated). We
                    # also don't want to move the pyre-strict since its already in
                    # the header, so don't mark that we need to move.
                    self.module_header_tags[tag] += 1
                    if self.module_header_tags[tag] > 1:
                        return libcst.RemoveFromParent()
                    else:
                        return updated_node.with_changes(
                            comment=libcst.Comment(f"# pyre-{tag}")
                        )
                else:
                    # We found an intended pyre-strict, but its spelled wrong. So, remove it
                    # and re-add a new one in leave_Module.
                    if self.module_header_tags[tag] < 1:
                        self.move_strict[tag] = True
                    return libcst.RemoveFromParent()
        # We found a regular comment, don't care about this.
        return updated_node

    def leave_Module(
        self, original_node: libcst.Module, updated_node: libcst.Module
    ) -> libcst.Module:
        comments = [f"# pyre-{tag}" for tag in self.PYRE_TAGS if self.move_strict[tag]]
        return insert_header_comments(updated_node, comments)
