# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#

from typing import Set, Tuple, Union

from libcst import Import, ImportFrom, ImportStar, Module
from libcst.codemod import CodemodContext, VisitorBasedCodemodCommand
from libcst.codemod.visitors import GatherCommentsVisitor, RemoveImportsVisitor
from libcst.helpers import get_absolute_module_from_package_for_import
from libcst.metadata import PositionProvider, ProviderT

DEFAULT_SUPPRESS_COMMENT_REGEX = (
    r".*\W(noqa|lint-ignore: ?unused-import|lint-ignore: ?F401)(\W.*)?$"
)


class RemoveUnusedImportsCommand(VisitorBasedCodemodCommand):
    """
    Remove all unused imports from a file based on scope analysis.

    This command analyses individual files in isolation and does not attempt
    to track cross-references between them. If a symbol is imported in a file
    but otherwise unused in it, that import will be removed even if it is being
    referenced from another file.
    """

    DESCRIPTION: str = (
        "Remove all imports that are not used in a file. "
        + "Note: only considers the file in isolation. "
    )

    METADATA_DEPENDENCIES: Tuple[ProviderT] = (PositionProvider,)

    def __init__(self, context: CodemodContext) -> None:
        super().__init__(context)
        self._ignored_lines: Set[int] = set()

    def visit_Module(self, node: Module) -> bool:
        comment_visitor = GatherCommentsVisitor(
            self.context, DEFAULT_SUPPRESS_COMMENT_REGEX
        )
        node.visit(comment_visitor)
        self._ignored_lines = set(comment_visitor.comments.keys())
        return True

    def visit_Import(self, node: Import) -> bool:
        self._handle_import(node)
        return False

    def visit_ImportFrom(self, node: ImportFrom) -> bool:
        self._handle_import(node)
        return False

    def _handle_import(self, node: Union[Import, ImportFrom]) -> None:
        node_start = self.get_metadata(PositionProvider, node).start.line
        if node_start in self._ignored_lines:
            return

        names = node.names
        if isinstance(names, ImportStar):
            return

        for alias in names:
            position = self.get_metadata(PositionProvider, alias)
            lines = set(range(position.start.line, position.end.line + 1))
            if lines.isdisjoint(self._ignored_lines):
                if isinstance(node, Import):
                    RemoveImportsVisitor.remove_unused_import(
                        self.context,
                        module=alias.evaluated_name,
                        asname=alias.evaluated_alias,
                    )
                else:
                    module_name = get_absolute_module_from_package_for_import(
                        self.context.full_package_name, node
                    )
                    if module_name is None:
                        raise ValueError(
                            f"Couldn't get absolute module name for {alias.evaluated_name}"
                        )
                    RemoveImportsVisitor.remove_unused_import(
                        self.context,
                        module=module_name,
                        obj=alias.evaluated_name,
                        asname=alias.evaluated_alias,
                    )
