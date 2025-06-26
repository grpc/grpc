# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from typing import Dict, List, Sequence, Set, Tuple, Union

import libcst
from libcst.codemod._context import CodemodContext
from libcst.codemod._visitor import ContextAwareVisitor
from libcst.codemod.visitors._imports import ImportItem
from libcst.helpers import get_absolute_module_from_package_for_import


class _GatherImportsMixin(ContextAwareVisitor):
    """
    A Mixin class for tracking visited imports.
    """

    def __init__(self, context: CodemodContext) -> None:
        super().__init__(context)
        # Track the available imports in this transform
        self.module_imports: Set[str] = set()
        self.object_mapping: Dict[str, Set[str]] = {}
        # Track the aliased imports in this transform
        self.module_aliases: Dict[str, str] = {}
        self.alias_mapping: Dict[str, List[Tuple[str, str]]] = {}
        # Track the import for every symbol introduced into the module
        self.symbol_mapping: Dict[str, ImportItem] = {}

    def _handle_Import(self, node: libcst.Import) -> None:
        for name in node.names:
            alias = name.evaluated_alias
            imp = ImportItem(name.evaluated_name, alias=alias)
            if alias is not None:
                # Track this as an aliased module
                self.module_aliases[name.evaluated_name] = alias
                self.symbol_mapping[alias] = imp
            else:
                # Get the module we're importing as a string.
                self.module_imports.add(name.evaluated_name)
                self.symbol_mapping[name.evaluated_name] = imp

    def _handle_ImportFrom(self, node: libcst.ImportFrom) -> None:
        # Get the module we're importing as a string.
        module = get_absolute_module_from_package_for_import(
            self.context.full_package_name, node
        )
        if module is None:
            # Can't get the absolute import from relative, so we can't
            # support this.
            return
        nodenames = node.names
        if isinstance(nodenames, libcst.ImportStar):
            # We cover everything, no need to bother tracking other things
            self.object_mapping[module] = set("*")
            return
        elif isinstance(nodenames, Sequence):
            # Get the list of imports we're aliasing in this import
            new_aliases = [
                (ia.evaluated_name, ia.evaluated_alias)
                for ia in nodenames
                if ia.asname is not None
            ]
            if new_aliases:
                if module not in self.alias_mapping:
                    self.alias_mapping[module] = []
                # pyre-ignore We know that aliases are not None here.
                self.alias_mapping[module].extend(new_aliases)

            # Get the list of imports we're importing in this import
            new_objects = {ia.evaluated_name for ia in nodenames if ia.asname is None}
            if new_objects:
                if module not in self.object_mapping:
                    self.object_mapping[module] = set()

                # Make sure that we don't add to a '*' module
                if "*" in self.object_mapping[module]:
                    self.object_mapping[module] = set("*")
                    return

                self.object_mapping[module].update(new_objects)
            for ia in nodenames:
                imp = ImportItem(
                    module, obj_name=ia.evaluated_name, alias=ia.evaluated_alias
                )
                key = ia.evaluated_alias or ia.evaluated_name
                self.symbol_mapping[key] = imp


class GatherImportsVisitor(_GatherImportsMixin):
    """
    Gathers all imports in a module and stores them as attributes on the instance.
    Intended to be instantiated and passed to a :class:`~libcst.Module`
    :meth:`~libcst.CSTNode.visit` method in order to gather up information about
    imports on a module. Note that this is not a substitute for scope analysis or
    qualified name support. Please see :ref:`libcst-scope-tutorial` for a more
    robust way of determining the qualified name and definition for an arbitrary
    node.

    After visiting a module the following attributes will be populated:

     module_imports
      A sequence of strings representing modules that were imported directly, such as
      in the case of ``import typing``. Each module directly imported but not aliased
      will be included here.
     object_mapping
      A mapping of strings to sequences of strings representing modules where we
      imported objects from, such as in the case of ``from typing import Optional``.
      Each from import that was not aliased will be included here, where the keys of
      the mapping are the module we are importing from, and the value is a
      sequence of objects we are importing from the module.
     module_aliases
      A mapping of strings representing modules that were imported and aliased,
      such as in the case of ``import typing as t``. Each module imported this
      way will be represented as a key in this mapping, and the value will be
      the local alias of the module.
     alias_mapping
      A mapping of strings to sequences of tuples representing modules where we
      imported objects from and aliased using ``as`` syntax, such as in the case
      of ``from typing import Optional as opt``. Each from import that was aliased
      will be included here, where the keys of the mapping are the module we are
      importing from, and the value is a tuple representing the original object
      name and the alias.
     all_imports
      A collection of all :class:`~libcst.Import` and :class:`~libcst.ImportFrom`
      statements that were encountered in the module.
    """

    def __init__(self, context: CodemodContext) -> None:
        super().__init__(context)
        # Track all of the imports found in this transform
        self.all_imports: List[Union[libcst.Import, libcst.ImportFrom]] = []

    def visit_Import(self, node: libcst.Import) -> None:
        # Track this import statement for later analysis.
        self.all_imports.append(node)
        self._handle_Import(node)

    def visit_ImportFrom(self, node: libcst.ImportFrom) -> None:
        # Track this import statement for later analysis.
        self.all_imports.append(node)
        self._handle_ImportFrom(node)
