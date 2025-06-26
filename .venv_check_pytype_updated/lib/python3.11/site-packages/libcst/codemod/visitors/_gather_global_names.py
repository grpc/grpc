# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Set

import libcst
from libcst.codemod._context import CodemodContext
from libcst.codemod._visitor import ContextAwareVisitor


class GatherGlobalNamesVisitor(ContextAwareVisitor):
    """
    Gathers all globally accessible names defined in a module and stores them as
    attributes on the instance.
    Intended to be instantiated and passed to a :class:`~libcst.Module`
    :meth:`~libcst.CSTNode.visit` method in order to gather up information about
    names defined on a module. Note that this is not a substitute for scope
    analysis or qualified name support. Please see :ref:`libcst-scope-tutorial`
    for a more robust way of determining the qualified name and definition for
    an arbitrary node.
    Names that are globally accessible through imports are currently not included
    but can be retrieved with GatherImportsVisitor.

    After visiting a module the following attributes will be populated:

     global_names
      A sequence of strings representing global variables defined in the module
      toplevel.
     class_names
      A sequence of strings representing classes defined in the module toplevel.
     function_names
      A sequence of strings representing functions defined in the module toplevel.

    """

    def __init__(self, context: CodemodContext) -> None:
        super().__init__(context)
        self.global_names: Set[str] = set()
        self.class_names: Set[str] = set()
        self.function_names: Set[str] = set()
        # Track scope nesting
        self.scope_depth: int = 0

    def visit_ClassDef(self, node: libcst.ClassDef) -> None:
        if self.scope_depth == 0:
            self.class_names.add(node.name.value)
        self.scope_depth += 1

    def leave_ClassDef(self, original_node: libcst.ClassDef) -> None:
        self.scope_depth -= 1

    def visit_FunctionDef(self, node: libcst.FunctionDef) -> None:
        if self.scope_depth == 0:
            self.function_names.add(node.name.value)
        self.scope_depth += 1

    def leave_FunctionDef(self, original_node: libcst.FunctionDef) -> None:
        self.scope_depth -= 1

    def visit_Assign(self, node: libcst.Assign) -> None:
        if self.scope_depth != 0:
            return
        for assign_target in node.targets:
            target = assign_target.target
            if isinstance(target, libcst.Name):
                self.global_names.add(target.value)

    def visit_AnnAssign(self, node: libcst.AnnAssign) -> None:
        if self.scope_depth != 0:
            return
        target = node.target
        if isinstance(target, libcst.Name):
            self.global_names.add(target.value)
