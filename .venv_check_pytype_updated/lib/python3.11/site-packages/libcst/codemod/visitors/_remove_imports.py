# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from typing import Any, Dict, Iterable, List, Optional, Sequence, Set, Tuple, Union

import libcst as cst
from libcst import CSTLogicError
from libcst.codemod._context import CodemodContext
from libcst.codemod._visitor import ContextAwareTransformer, ContextAwareVisitor
from libcst.codemod.visitors._gather_unused_imports import GatherUnusedImportsVisitor
from libcst.helpers import (
    get_absolute_module_from_package_for_import,
    get_full_name_for_node,
)
from libcst.metadata import Assignment, ProviderT, ScopeProvider


class RemovedNodeVisitor(ContextAwareVisitor):
    def _remove_imports_from_import_stmt(
        self, local_name: str, import_node: cst.Import
    ) -> None:
        for import_alias in import_node.names:
            if import_alias.evaluated_alias is None:
                prefix = import_alias.evaluated_name
            else:
                prefix = import_alias.evaluated_alias

            if local_name == prefix or local_name.startswith(f"{prefix}."):
                RemoveImportsVisitor.remove_unused_import(
                    self.context,
                    import_alias.evaluated_name,
                    asname=import_alias.evaluated_alias,
                )

    def _remove_imports_from_importfrom_stmt(
        self, local_name: str, import_node: cst.ImportFrom
    ) -> None:
        names = import_node.names
        if isinstance(names, cst.ImportStar):
            # We don't handle removing this, so ignore it.
            return

        module_name = get_absolute_module_from_package_for_import(
            self.context.full_package_name, import_node
        )
        if module_name is None:
            raise ValueError("Cannot look up absolute module from relative import!")

        # We know any local names will refer to this as an alias if
        # there is one, and as the original name if there is not one
        for import_alias in names:
            if import_alias.evaluated_alias is None:
                prefix = import_alias.evaluated_name
            else:
                prefix = import_alias.evaluated_alias

            if local_name == prefix or local_name.startswith(f"{prefix}."):
                RemoveImportsVisitor.remove_unused_import(
                    self.context,
                    module_name,
                    obj=import_alias.evaluated_name,
                    asname=import_alias.evaluated_alias,
                )

    def _visit_name_attr_alike(self, node: Union[cst.Name, cst.Attribute]) -> None:
        # Look up the local name of this node.
        local_name = get_full_name_for_node(node)
        if local_name is None:
            return

        # Look up the scope for this node, remove the import that caused it to exist.
        metadata_wrapper = self.context.wrapper
        if metadata_wrapper is None:
            raise ValueError(
                "Cannot look up import, metadata is not computed for node!"
            )
        scope_provider = metadata_wrapper.resolve(ScopeProvider)
        try:
            scope = scope_provider[node]
            if scope is None:
                # This object has no scope, so we can't remove it.
                return
        except KeyError:
            # This object has no scope, so we can't remove it.
            return

        while True:
            for assignment in scope.assignments[node] or set():
                # We only care about non-builtins.
                if isinstance(assignment, Assignment):
                    import_node = assignment.node
                    if isinstance(import_node, cst.Import):
                        self._remove_imports_from_import_stmt(local_name, import_node)
                    elif isinstance(import_node, cst.ImportFrom):
                        self._remove_imports_from_importfrom_stmt(
                            local_name, import_node
                        )

            if scope is scope.parent:
                break
            scope = scope.parent

    def visit_Name(self, node: cst.Name) -> None:
        self._visit_name_attr_alike(node)

    def visit_Attribute(self, node: cst.Attribute) -> None:
        self._visit_name_attr_alike(node)


class RemoveImportsVisitor(ContextAwareTransformer):
    """
    Attempt to remove given imports from a module, dependent on whether there are
    any uses of the imported objects. Given a :class:`~libcst.codemod.CodemodContext`
    and a sequence of tuples specifying a module to remove as a string. Optionally
    an object being imported from that module and optionally an alias assigned to
    that imported object, ensures that that import no longer exists as long as there
    are no remaining references.

    Note that static analysis is able to determine safely whether an import is still
    needed given a particular module, but it is currently unable to determine whether
    an imported object is re-exported and used inside another module unless that
    object appears in an ``__any__`` list.

    This is one of the transforms that is available automatically to you when running
    a codemod. To use it in this manner, import
    :class:`~libcst.codemod.visitors.RemoveImportsVisitor` and then call the static
    :meth:`~libcst.codemod.visitors.RemoveImportsVisitor.remove_unused_import` method,
    giving it the current context (found as ``self.context`` for all subclasses of
    :class:`~libcst.codemod.Codemod`), the module you wish to remove and
    optionally an object you wish to stop importing as well as an alias that the
    object is currently assigned to.

    For example::

        RemoveImportsVisitor.remove_unused_import(self.context, "typing", "Optional")

    This will remove any ``from typing import Optional`` that exists in the module
    as long as there are no uses of ``Optional`` in that module.

    As another example::

        RemoveImportsVisitor.remove_unused_import(self.context, "typing")

    This will remove any ``import typing`` that exists in the module, as long as
    there are no references to ``typing`` in that module, including references
    such as ``typing.Optional``.

    Additionally, :class:`~libcst.codemod.visitors.RemoveImportsVisitor` includes
    a convenience function
    :meth:`~libcst.codemod.visitors.RemoveImportsVisitor.remove_unused_import_by_node`
    which will attempt to schedule removal of all imports referenced in that node
    and its children. This is especially useful inside transforms when you are going
    to remove a node using :func:`~libcst.RemoveFromParent` to get rid of a node.

    For example::

        def leave_AnnAssign(
            self, original_node: cst.AnnAssign, updated_node: cst.AnnAssign,
        ) -> cst.RemovalSentinel:
            # Remove all annotated assignment statements, clean up imports.
            RemoveImportsVisitor.remove_unused_import_by_node(self.context, original_node)
            return cst.RemovalFromParent()

    This will remove all annotated assignment statements from a module as well
    as clean up any imports that were only referenced in those assignments. Note
    that we pass the ``original_node`` to the helper function as it uses scope analysis
    under the hood which is only computed on the original tree.

    Note that this is a subclass of :class:`~libcst.CSTTransformer` so it is
    possible to instantiate it and pass it to a :class:`~libcst.Module`
    :meth:`~libcst.CSTNode.visit` method. However, it is far easier to use
    the automatic transform feature of :class:`~libcst.codemod.CodemodCommand`
    and schedule an import to be added by calling
    :meth:`~libcst.codemod.visitors.RemoveImportsVisitor.remove_unused_import`

    """

    CONTEXT_KEY = "RemoveImportsVisitor"
    METADATA_DEPENDENCIES: Tuple[ProviderT] = (
        *GatherUnusedImportsVisitor.METADATA_DEPENDENCIES,
    )

    @staticmethod
    def _get_imports_from_context(
        context: CodemodContext,
    ) -> List[Tuple[str, Optional[str], Optional[str]]]:
        unused_imports = context.scratch.get(RemoveImportsVisitor.CONTEXT_KEY, [])
        if not isinstance(unused_imports, list):
            raise CSTLogicError("Logic error!")
        return unused_imports

    @staticmethod
    def remove_unused_import(
        context: CodemodContext,
        module: str,
        obj: Optional[str] = None,
        asname: Optional[str] = None,
    ) -> None:
        """
        Schedule an import to be removed in a future invocation of this class by
        updating the ``context`` to include the ``module`` and optionally ``obj``
        which is currently imported as well as optionally ``alias`` that the
        imported ``module`` or ``obj`` is aliased to. When subclassing from
        :class:`~libcst.codemod.CodemodCommand`, this will be performed for you
        after your transform finishes executing. If you are subclassing from a
        :class:`~libcst.codemod.Codemod` instead, you will need to call the
        :meth:`~libcst.codemod.Codemod.transform_module` method on the module
        under modification with an instance of this class after performing your
        transform. Note that if the particular ``module`` or ``obj`` you are
        requesting to remove is still in use somewhere in the current module
        at the time of executing :meth:`~libcst.codemod.Codemod.transform_module`
        on an instance of :class:`~libcst.codemod.visitors.AddImportsVisitor`,
        this will perform no action in order to avoid removing an in-use import.
        """

        unused_imports = RemoveImportsVisitor._get_imports_from_context(context)
        unused_imports.append((module, obj, asname))
        context.scratch[RemoveImportsVisitor.CONTEXT_KEY] = unused_imports

    @staticmethod
    def remove_unused_import_by_node(
        context: CodemodContext, node: cst.CSTNode
    ) -> None:
        """
        Schedule any imports referenced by ``node`` or one of its children
        to be removed in a future invocation of this class by updating the
        ``context`` to include the ``module``, ``obj`` and ``alias`` for each
        import in question. When subclassing from
        :class:`~libcst.codemod.CodemodCommand`, this will be performed for you
        after your transform finishes executing. If you are subclassing from a
        :class:`~libcst.codemod.Codemod` instead, you will need to call the
        :meth:`~libcst.codemod.Codemod.transform_module` method on the module
        under modification with an instance of this class after performing your
        transform. Note that all imports that are referenced by this ``node``
        or its children will only be removed if they are not in use at the time
        of exeucting :meth:`~libcst.codemod.Codemod.transform_module`
        on an instance of :class:`~libcst.codemod.visitors.AddImportsVisitor`
        in order to avoid removing an in-use import.
        """

        # Special case both Import and ImportFrom so they can be
        # directly removed here.
        if isinstance(node, cst.Import):
            for import_alias in node.names:
                RemoveImportsVisitor.remove_unused_import(
                    context,
                    import_alias.evaluated_name,
                    asname=import_alias.evaluated_alias,
                )
        elif isinstance(node, cst.ImportFrom):
            names = node.names
            if isinstance(names, cst.ImportStar):
                # We don't handle removing this, so ignore it.
                return
            module_name = get_absolute_module_from_package_for_import(
                context.full_package_name, node
            )
            if module_name is None:
                raise ValueError("Cannot look up absolute module from relative import!")
            for import_alias in names:
                RemoveImportsVisitor.remove_unused_import(
                    context,
                    module_name,
                    obj=import_alias.evaluated_name,
                    asname=import_alias.evaluated_alias,
                )
        else:
            # Look up all children that could have been imported. Any that
            # we find will be scheduled for removal.
            node.visit(RemovedNodeVisitor(context))

    def __init__(
        self,
        context: CodemodContext,
        unused_imports: Sequence[Tuple[str, Optional[str], Optional[str]]] = (),
    ) -> None:
        # Allow for instantiation from either a context (used when multiple transforms
        # get chained) or from a direct instantiation.
        super().__init__(context)

        all_unused_imports: List[Tuple[str, Optional[str], Optional[str]]] = [
            *RemoveImportsVisitor._get_imports_from_context(context),
            *unused_imports,
        ]
        self.unused_module_imports: Dict[str, Optional[str]] = {
            module: alias for module, obj, alias in all_unused_imports if obj is None
        }
        self.unused_obj_imports: Dict[str, Set[Tuple[str, Optional[str]]]] = {}
        for module, obj, alias in all_unused_imports:
            if obj is None:
                continue
            if module not in self.unused_obj_imports:
                self.unused_obj_imports[module] = set()
            self.unused_obj_imports[module].add((obj, alias))
        self._unused_imports: Dict[
            cst.ImportAlias, Union[cst.Import, cst.ImportFrom]
        ] = {}

    def visit_Module(self, node: cst.Module) -> None:
        visitor = GatherUnusedImportsVisitor(self.context)
        node.visit(visitor)
        self._unused_imports = {k: v for (k, v) in visitor.unused_imports}

    def leave_Import(
        self, original_node: cst.Import, updated_node: cst.Import
    ) -> Union[cst.Import, cst.RemovalSentinel]:
        names_to_keep = []
        for import_alias in original_node.names:
            if import_alias.evaluated_name not in self.unused_module_imports:
                # This is a keeper since we aren't removing it
                names_to_keep.append(import_alias)
                continue

            if (
                import_alias.evaluated_alias
                != self.unused_module_imports[import_alias.evaluated_name]
            ):
                # This is a keeper since the alias does not match
                # what we are looking for.
                names_to_keep.append(import_alias)
                continue

            # Now that we know we want to remove this module, figure out if
            # there are any live references to it.
            if import_alias not in self._unused_imports:
                names_to_keep.append(import_alias)
                continue

        # no changes
        if names_to_keep == original_node.names:
            return updated_node

        # Now, either remove this statement or remove the imports we are
        # deleting from this statement.
        if len(names_to_keep) == 0:
            return cst.RemoveFromParent()

        if names_to_keep[-1] != original_node.names[-1]:
            # Remove trailing comma in order to not mess up import statements.
            names_to_keep = [
                *names_to_keep[:-1],
                names_to_keep[-1].with_changes(comma=cst.MaybeSentinel.DEFAULT),
            ]
        return updated_node.with_changes(names=names_to_keep)

    def _process_importfrom_aliases(
        self,
        updated_node: cst.ImportFrom,
        names: Iterable[cst.ImportAlias],
        module_name: str,
    ) -> Dict[str, Any]:
        updates = {}
        names_to_keep = []
        objects_to_remove = self.unused_obj_imports[module_name]
        for import_alias in names:
            # Figure out if it is in our list of things to kill
            for name, alias in objects_to_remove:
                if (
                    name == import_alias.evaluated_name
                    and alias == import_alias.evaluated_alias
                ):
                    break
            else:
                # This is a keeper, we don't have it on our list.
                names_to_keep.append(import_alias)
                continue

            # Now that we know we want to remove this object, figure out if
            # there are any live references to it.
            if import_alias not in self._unused_imports:
                names_to_keep.append(import_alias)
                continue

            # We are about to remove `import_alias`. Check if there are any
            # trailing comments and reparent them to the previous import.
            # We only do this in case there's a trailing comma, otherwise the
            # entire import statement is going to be removed anyway.
            comma = import_alias.comma
            if isinstance(comma, cst.Comma):
                if len(names_to_keep) != 0:
                    # there is a previous import alias
                    prev = names_to_keep[-1]
                    if isinstance(prev.comma, cst.Comma):
                        prev = prev.with_deep_changes(
                            prev.comma,
                            whitespace_after=_merge_whitespace_after(
                                prev.comma.whitespace_after,
                                comma.whitespace_after,
                            ),
                        )
                    else:
                        # The previous alias didn't have a trailing comma. This can
                        # occur if the alias was generated, instead of being parsed
                        # from source.
                        prev = prev.with_changes(comma=comma)
                    names_to_keep[-1] = prev
                else:
                    # No previous import alias, need to attach comment to `ImportFrom`.
                    # We can only do this if there was a leftparen on the import
                    # statement. Otherwise there can't be any standalone comments
                    # anyway, so it's fine to skip this logic.
                    lpar = updated_node.lpar
                    if isinstance(lpar, cst.LeftParen):
                        updates["lpar"] = lpar.with_changes(
                            whitespace_after=_merge_whitespace_after(
                                lpar.whitespace_after,
                                comma.whitespace_after,
                            )
                        )
        updates["names"] = names_to_keep
        return updates

    def leave_ImportFrom(
        self, original_node: cst.ImportFrom, updated_node: cst.ImportFrom
    ) -> Union[cst.ImportFrom, cst.RemovalSentinel]:
        names = original_node.names
        if isinstance(names, cst.ImportStar):
            # This is a star import, so we won't remove it.
            return updated_node

        # Make sure we actually know the absolute module.
        module_name = get_absolute_module_from_package_for_import(
            self.context.full_package_name, updated_node
        )
        if module_name is None or module_name not in self.unused_obj_imports:
            # This node isn't on our list of todos, so let's bail.
            return updated_node

        updates = self._process_importfrom_aliases(updated_node, names, module_name)
        names_to_keep = updates["names"]

        # no changes
        if names_to_keep == names:
            return updated_node

        # Now, either remove this statement or remove the imports we are
        # deleting from this statement.
        if len(names_to_keep) == 0:
            return cst.RemoveFromParent()

        if names_to_keep[-1] != names[-1]:
            # Remove trailing comma in order to not mess up import statements.
            names_to_keep = [
                *names_to_keep[:-1],
                names_to_keep[-1].with_changes(comma=cst.MaybeSentinel.DEFAULT),
            ]
        updates["names"] = names_to_keep
        return updated_node.with_changes(**updates)


def _merge_whitespace_after(
    left: cst.BaseParenthesizableWhitespace, right: cst.BaseParenthesizableWhitespace
) -> cst.BaseParenthesizableWhitespace:
    if not isinstance(right, cst.ParenthesizedWhitespace):
        return left
    if not isinstance(left, cst.ParenthesizedWhitespace):
        return right

    return left.with_changes(
        empty_lines=tuple(
            line for line in right.empty_lines if line.comment is not None
        ),
    )
