# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from collections import defaultdict
from typing import Dict, List, Optional, Sequence, Set, Tuple, Union

import libcst
from libcst import CSTLogicError, matchers as m, parse_statement
from libcst._nodes.statement import Import, ImportFrom, SimpleStatementLine
from libcst.codemod._context import CodemodContext
from libcst.codemod._visitor import ContextAwareTransformer
from libcst.codemod.visitors._gather_imports import _GatherImportsMixin
from libcst.codemod.visitors._imports import ImportItem
from libcst.helpers import get_absolute_module_from_package_for_import
from libcst.helpers.common import ensure_type


class _GatherTopImportsBeforeStatements(_GatherImportsMixin):
    """
    Works similarly to GatherImportsVisitor, but only considers imports
    declared before any other statements of the module with the exception
    of docstrings and __strict__ flag.
    """

    def __init__(self, context: CodemodContext) -> None:
        super().__init__(context)
        # Track all of the imports found in this transform
        self.all_imports: List[Union[libcst.Import, libcst.ImportFrom]] = []

    def leave_Module(self, original_node: libcst.Module) -> None:
        start = 1 if _skip_first(original_node) else 0
        for stmt in original_node.body[start:]:
            if m.matches(
                stmt,
                m.SimpleStatementLine(body=[m.ImportFrom() | m.Import()]),
            ):
                stmt = ensure_type(stmt, SimpleStatementLine)
                # Workaround for python 3.8 and 3.9, won't accept Union for isinstance
                if m.matches(stmt.body[0], m.ImportFrom()):
                    imp = ensure_type(stmt.body[0], ImportFrom)
                    self.all_imports.append(imp)
                if m.matches(stmt.body[0], m.Import()):
                    imp = ensure_type(stmt.body[0], Import)
                    self.all_imports.append(imp)
            else:
                break
        for imp in self.all_imports:
            if m.matches(imp, m.Import()):
                imp = ensure_type(imp, Import)
                self._handle_Import(imp)
            else:
                imp = ensure_type(imp, ImportFrom)
                self._handle_ImportFrom(imp)


class AddImportsVisitor(ContextAwareTransformer):
    """
    Ensures that given imports exist in a module. Given a
    :class:`~libcst.codemod.CodemodContext` and a sequence of tuples specifying
    a module to import from as a string. Optionally an object to import from
    that module and any alias to assign that import, ensures that import exists.
    It will modify existing imports as necessary if the module in question is
    already being imported from.

    This is one of the transforms that is available automatically to you when
    running a codemod. To use it in this manner, import
    :class:`~libcst.codemod.visitors.AddImportsVisitor` and then call the static
    :meth:`~libcst.codemod.visitors.AddImportsVisitor.add_needed_import` method,
    giving it the current context (found as ``self.context`` for all subclasses of
    :class:`~libcst.codemod.Codemod`), the module you wish to import from and
    optionally an object you wish to import from that module and any alias you
    would like to assign that import to.

    For example::

        AddImportsVisitor.add_needed_import(self.context, "typing", "Optional")

    This will produce the following code in a module, assuming there was no
    typing import already::

        from typing import Optional

    As another example::

        AddImportsVisitor.add_needed_import(self.context, "typing")

    This will produce the following code in a module, assuming there was no
    import already::

        import typing

    Note that this is a subclass of :class:`~libcst.CSTTransformer` so it is
    possible to instantiate it and pass it to a :class:`~libcst.Module`
    :meth:`~libcst.CSTNode.visit` method. However, it is far easier to use
    the automatic transform feature of :class:`~libcst.codemod.CodemodCommand`
    and schedule an import to be added by calling
    :meth:`~libcst.codemod.visitors.AddImportsVisitor.add_needed_import`
    """

    CONTEXT_KEY = "AddImportsVisitor"

    @staticmethod
    def _get_imports_from_context(
        context: CodemodContext,
    ) -> List[ImportItem]:
        imports = context.scratch.get(AddImportsVisitor.CONTEXT_KEY, [])
        if not isinstance(imports, list):
            raise CSTLogicError("Logic error!")
        return imports

    @staticmethod
    def add_needed_import(
        context: CodemodContext,
        module: str,
        obj: Optional[str] = None,
        asname: Optional[str] = None,
        relative: int = 0,
    ) -> None:
        """
        Schedule an import to be added in a future invocation of this class by
        updating the ``context`` to include the ``module`` and optionally ``obj``
        to be imported as well as optionally ``alias`` to alias the imported
        ``module`` or ``obj`` to. When subclassing from
        :class:`~libcst.codemod.CodemodCommand`, this will be performed for you
        after your transform finishes executing. If you are subclassing from a
        :class:`~libcst.codemod.Codemod` instead, you will need to call the
        :meth:`~libcst.codemod.Codemod.transform_module` method on the module
        under modification with an instance of this class after performing your
        transform. Note that if the particular ``module`` or ``obj`` you are
        requesting to import already exists as an import on the current module
        at the time of executing :meth:`~libcst.codemod.Codemod.transform_module`
        on an instance of :class:`~libcst.codemod.visitors.AddImportsVisitor`,
        this will perform no action in order to avoid adding duplicate imports.
        """

        if module == "__future__" and obj is None:
            raise ValueError("Cannot import __future__ directly!")
        imports = AddImportsVisitor._get_imports_from_context(context)
        imports.append(ImportItem(module, obj, asname, relative))
        context.scratch[AddImportsVisitor.CONTEXT_KEY] = imports

    def __init__(
        self,
        context: CodemodContext,
        imports: Sequence[ImportItem] = (),
    ) -> None:
        # Allow for instantiation from either a context (used when multiple transforms
        # get chained) or from a direct instantiation.
        super().__init__(context)
        imps: List[ImportItem] = [
            *AddImportsVisitor._get_imports_from_context(context),
            *imports,
        ]

        # Verify that the imports are valid
        for imp in imps:
            if imp.module == "__future__" and imp.obj_name is None:
                raise ValueError("Cannot import __future__ directly!")
            if imp.module == "__future__" and imp.alias is not None:
                raise ValueError("Cannot import __future__ objects with aliases!")

        # Resolve relative imports if we have a module name
        imps = [imp.resolve_relative(self.context.full_package_name) for imp in imps]

        # List of modules we need to ensure are imported
        self.module_imports: Set[str] = {
            imp.module for imp in imps if imp.obj_name is None and imp.alias is None
        }

        # List of modules we need to check for object imports on
        from_imports: Set[str] = {
            imp.module for imp in imps if imp.obj_name is not None and imp.alias is None
        }
        # Mapping of modules we're adding to the object they should import
        self.module_mapping: Dict[str, Set[str]] = {
            module: {
                imp.obj_name
                for imp in imps
                if imp.module == module
                and imp.obj_name is not None
                and imp.alias is None
            }
            for module in sorted(from_imports)
        }

        # List of aliased modules we need to ensure are imported
        self.module_aliases: Dict[str, str] = {
            imp.module: imp.alias
            for imp in imps
            if imp.obj_name is None and imp.alias is not None
        }
        # List of modules we need to check for object imports on
        from_imports_aliases: Set[str] = {
            imp.module
            for imp in imps
            if imp.obj_name is not None and imp.alias is not None
        }
        # Mapping of modules we're adding to the object with alias they should import
        self.alias_mapping: Dict[str, List[Tuple[str, str]]] = {
            module: [
                (imp.obj_name, imp.alias)
                for imp in imps
                if imp.module == module
                and imp.obj_name is not None
                and imp.alias is not None
            ]
            for module in sorted(from_imports_aliases)
        }

        # Track the list of imports found at the top of the file
        self.all_imports: List[Union[libcst.Import, libcst.ImportFrom]] = []

    def visit_Module(self, node: libcst.Module) -> None:
        # Do a preliminary pass to gather the imports we already have at the top
        gatherer = _GatherTopImportsBeforeStatements(self.context)
        node.visit(gatherer)
        self.all_imports = gatherer.all_imports

        self.module_imports = self.module_imports - gatherer.module_imports
        for module, alias in gatherer.module_aliases.items():
            if module in self.module_aliases and self.module_aliases[module] == alias:
                del self.module_aliases[module]
        for module, aliases in gatherer.alias_mapping.items():
            for obj, alias in aliases:
                if (
                    module in self.alias_mapping
                    and (obj, alias) in self.alias_mapping[module]
                ):
                    self.alias_mapping[module].remove((obj, alias))
                    if len(self.alias_mapping[module]) == 0:
                        del self.alias_mapping[module]

        for module, imports in gatherer.object_mapping.items():
            if module not in self.module_mapping:
                # We don't care about this import at all
                continue
            elif "*" in imports:
                # We already implicitly are importing everything
                del self.module_mapping[module]
            else:
                # Lets figure out what's left to import
                self.module_mapping[module] = self.module_mapping[module] - imports
                if not self.module_mapping[module]:
                    # There's nothing left, so lets delete this work item
                    del self.module_mapping[module]

    def leave_ImportFrom(
        self, original_node: libcst.ImportFrom, updated_node: libcst.ImportFrom
    ) -> libcst.ImportFrom:
        if isinstance(updated_node.names, libcst.ImportStar):
            # There's nothing to do here!
            return updated_node

        # Ensure this is one of the imports at the top
        if original_node not in self.all_imports:
            return updated_node

        # Get the module we're importing as a string, see if we have work to do.
        module = get_absolute_module_from_package_for_import(
            self.context.full_package_name, updated_node
        )
        if (
            module is None
            or module not in self.module_mapping
            and module not in self.alias_mapping
        ):
            return updated_node

        # We have work to do, mark that we won't modify this again.
        imports_to_add = self.module_mapping.get(module, [])
        if module in self.module_mapping:
            del self.module_mapping[module]
        aliases_to_add = self.alias_mapping.get(module, [])
        if module in self.alias_mapping:
            del self.alias_mapping[module]

        # Now, do the actual update.
        return updated_node.with_changes(
            names=[
                *(
                    libcst.ImportAlias(name=libcst.Name(imp))
                    for imp in sorted(imports_to_add)
                ),
                *(
                    libcst.ImportAlias(
                        name=libcst.Name(imp),
                        asname=libcst.AsName(name=libcst.Name(alias)),
                    )
                    for (imp, alias) in sorted(aliases_to_add)
                ),
                *updated_node.names,
            ]
        )

    def _split_module(
        self, orig_module: libcst.Module, updated_module: libcst.Module
    ) -> Tuple[
        List[Union[libcst.SimpleStatementLine, libcst.BaseCompoundStatement]],
        List[Union[libcst.SimpleStatementLine, libcst.BaseCompoundStatement]],
        List[Union[libcst.SimpleStatementLine, libcst.BaseCompoundStatement]],
    ]:
        statement_before_import_location = 0
        import_add_location = 0

        # This works under the principle that while we might modify node contents,
        # we have yet to modify the number of statements. So we can match on the
        # original tree but break up the statements of the modified tree. If we
        # change this assumption in this visitor, we will have to change this code.

        # Finds the location to add imports. It is the end of the first import block that occurs before any other statement (save for docstrings)

        # Never insert an import before initial __strict__ flag or docstring
        if _skip_first(orig_module):
            statement_before_import_location = import_add_location = 1

        for i, statement in enumerate(
            orig_module.body[statement_before_import_location:]
        ):
            if m.matches(
                statement, m.SimpleStatementLine(body=[m.ImportFrom() | m.Import()])
            ):
                import_add_location = i + statement_before_import_location + 1
            else:
                break

        return (
            list(updated_module.body[:statement_before_import_location]),
            list(
                updated_module.body[
                    statement_before_import_location:import_add_location
                ]
            ),
            list(updated_module.body[import_add_location:]),
        )

    def _insert_empty_line(
        self,
        statements: List[
            Union[libcst.SimpleStatementLine, libcst.BaseCompoundStatement]
        ],
    ) -> List[Union[libcst.SimpleStatementLine, libcst.BaseCompoundStatement]]:
        if len(statements) < 1:
            # No statements, nothing to add to
            return statements
        if len(statements[0].leading_lines) == 0:
            # Statement has no leading lines, add one!
            return [
                statements[0].with_changes(leading_lines=(libcst.EmptyLine(),)),
                *statements[1:],
            ]
        if statements[0].leading_lines[0].comment is None:
            # First line is empty, so its safe to leave as-is
            return statements
        # Statement has a comment first line, so lets add one more empty line
        return [
            statements[0].with_changes(
                leading_lines=(libcst.EmptyLine(), *statements[0].leading_lines)
            ),
            *statements[1:],
        ]

    def leave_Module(
        self, original_node: libcst.Module, updated_node: libcst.Module
    ) -> libcst.Module:
        # Don't try to modify if we have nothing to do
        if (
            not self.module_imports
            and not self.module_mapping
            and not self.module_aliases
            and not self.alias_mapping
        ):
            return updated_node

        # First, find the insertion point for imports
        (
            statements_before_imports,
            statements_until_add_imports,
            statements_after_imports,
        ) = self._split_module(original_node, updated_node)

        # Make sure there's at least one empty line before the first non-import
        statements_after_imports = self._insert_empty_line(statements_after_imports)

        # Mapping of modules we're adding to the object with and without alias they should import
        module_and_alias_mapping = defaultdict(list)
        for module, aliases in self.alias_mapping.items():
            module_and_alias_mapping[module].extend(aliases)
        for module, imports in self.module_mapping.items():
            module_and_alias_mapping[module].extend(
                [(object, None) for object in imports]
            )
        module_and_alias_mapping = {
            module: sorted(aliases)
            for module, aliases in module_and_alias_mapping.items()
        }
        # Now, add all of the imports we need!
        return updated_node.with_changes(
            # pyre-fixme[60]: Concatenation not yet support for multiple variadic tup...
            body=(
                *statements_before_imports,
                *[
                    parse_statement(
                        f"from {module} import "
                        + ", ".join(
                            [
                                obj if alias is None else f"{obj} as {alias}"
                                for (obj, alias) in aliases
                            ]
                        ),
                        config=updated_node.config_for_parsing,
                    )
                    for module, aliases in module_and_alias_mapping.items()
                    if module == "__future__"
                ],
                *statements_until_add_imports,
                *[
                    parse_statement(
                        f"import {module}", config=updated_node.config_for_parsing
                    )
                    for module in sorted(self.module_imports)
                ],
                *[
                    parse_statement(
                        f"import {module} as {asname}",
                        config=updated_node.config_for_parsing,
                    )
                    for (module, asname) in self.module_aliases.items()
                ],
                *[
                    parse_statement(
                        f"from {module} import "
                        + ", ".join(
                            [
                                obj if alias is None else f"{obj} as {alias}"
                                for (obj, alias) in aliases
                            ]
                        ),
                        config=updated_node.config_for_parsing,
                    )
                    for module, aliases in module_and_alias_mapping.items()
                    if module != "__future__"
                ],
                *statements_after_imports,
            )
        )


def _skip_first(orig_module: libcst.Module) -> bool:
    # Is there a __strict__ flag or docstring at the top?
    if m.matches(
        orig_module,
        m.Module(
            body=[
                m.SimpleStatementLine(
                    body=[
                        m.Assign(targets=[m.AssignTarget(target=m.Name("__strict__"))])
                    ]
                ),
                m.ZeroOrMore(),
            ]
        )
        | m.Module(
            body=[
                m.SimpleStatementLine(body=[m.Expr(value=m.SimpleString())]),
                m.ZeroOrMore(),
            ]
        ),
    ):
        return True
    return False
