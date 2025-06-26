# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#

from typing import Collection, Iterable, Set, Tuple, Union

import libcst as cst
from libcst.codemod._context import CodemodContext
from libcst.codemod._visitor import ContextAwareVisitor
from libcst.codemod.visitors._gather_exports import GatherExportsVisitor
from libcst.codemod.visitors._gather_string_annotation_names import (
    FUNCS_CONSIDERED_AS_STRING_ANNOTATIONS,
    GatherNamesFromStringAnnotationsVisitor,
)
from libcst.metadata import ProviderT, ScopeProvider
from libcst.metadata.scope_provider import _gen_dotted_names

MODULES_IGNORED_BY_DEFAULT = {"__future__"}


class GatherUnusedImportsVisitor(ContextAwareVisitor):
    """
    Collects all imports from a module not directly used in the same module.
    Intended to be instantiated and passed to a :class:`libcst.Module`
    :meth:`~libcst.CSTNode.visit` method to process the full module.

    Note that imports that are only used indirectly (from other modules) are
    still collected.

    After visiting a module the attribute ``unused_imports`` will contain a
    set of unused :class:`~libcst.ImportAlias` objects, paired with their
    parent import node.
    """

    # pyre-fixme[8]: Attribute has type
    #  `Tuple[typing.Type[cst.metadata.base_provider.BaseMetadataProvider[object]]]`;
    #  used as `Tuple[typing.Type[cst.metadata.name_provider.QualifiedNameProvider],
    #  typing.Type[cst.metadata.scope_provider.ScopeProvider]]`.
    METADATA_DEPENDENCIES: Tuple[ProviderT] = (
        *GatherNamesFromStringAnnotationsVisitor.METADATA_DEPENDENCIES,
        ScopeProvider,
    )

    def __init__(
        self,
        context: CodemodContext,
        ignored_modules: Collection[str] = MODULES_IGNORED_BY_DEFAULT,
        typing_functions: Collection[str] = FUNCS_CONSIDERED_AS_STRING_ANNOTATIONS,
    ) -> None:
        super().__init__(context)

        self._ignored_modules: Collection[str] = ignored_modules
        self._typing_functions = typing_functions
        self._string_annotation_names: Set[str] = set()
        self._exported_names: Set[str] = set()
        #: Contains a set of (alias, parent_import) pairs that are not used
        #: in the module after visiting.
        self.unused_imports: Set[
            Tuple[cst.ImportAlias, Union[cst.Import, cst.ImportFrom]]
        ] = set()

    def visit_Module(self, node: cst.Module) -> bool:
        export_collector = GatherExportsVisitor(self.context)
        node.visit(export_collector)
        self._exported_names = export_collector.explicit_exported_objects
        annotation_visitor = GatherNamesFromStringAnnotationsVisitor(
            self.context, typing_functions=self._typing_functions
        )
        node.visit(annotation_visitor)
        self._string_annotation_names = annotation_visitor.names
        return True

    def visit_Import(self, node: cst.Import) -> bool:
        self.handle_import(node)
        return False

    def visit_ImportFrom(self, node: cst.ImportFrom) -> bool:
        module = node.module
        if (
            not isinstance(node.names, cst.ImportStar)
            and module is not None
            and module.value not in self._ignored_modules
        ):
            self.handle_import(node)
        return False

    def handle_import(self, node: Union[cst.Import, cst.ImportFrom]) -> None:
        names = node.names
        assert not isinstance(names, cst.ImportStar)  # hello, type checker

        for alias in names:
            self.unused_imports.add((alias, node))

    def leave_Module(self, original_node: cst.Module) -> None:
        self.unused_imports = self.filter_unused_imports(self.unused_imports)

    def filter_unused_imports(
        self,
        candidates: Iterable[Tuple[cst.ImportAlias, Union[cst.Import, cst.ImportFrom]]],
    ) -> Set[Tuple[cst.ImportAlias, Union[cst.Import, cst.ImportFrom]]]:
        """
        Return the imports in ``candidates`` which are not used.

        This function implements the main logic of this visitor, and is called after traversal. It calls :meth:`~is_in_use` on each import.

        Override this in a subclass for additional filtering.
        """
        unused_imports = set()
        for alias, parent in candidates:
            scope = self.get_metadata(ScopeProvider, parent)
            if scope is None:
                continue
            if not self.is_in_use(scope, alias):
                unused_imports.add((alias, parent))
        return unused_imports

    def is_in_use(self, scope: cst.metadata.Scope, alias: cst.ImportAlias) -> bool:
        """
        Check if ``alias`` is in use in the given ``scope``.

        An alias is in use if it's directly referenced, exported, or appears in
        a string type annotation. Override this in a subclass for additional
        filtering.
        """
        asname = alias.asname
        names = _gen_dotted_names(
            cst.ensure_type(asname.name, cst.Name) if asname is not None else alias.name
        )

        for name_or_alias, _ in names:
            if (
                name_or_alias in self._exported_names
                or name_or_alias in self._string_annotation_names
            ):
                return True

            for assignment in scope[name_or_alias]:
                if (
                    isinstance(assignment, cst.metadata.ImportAssignment)
                    and len(assignment.references) > 0
                ):
                    return True
        return False
