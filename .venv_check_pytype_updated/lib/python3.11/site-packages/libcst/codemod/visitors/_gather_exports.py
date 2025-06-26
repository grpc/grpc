# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from typing import Set, Union

import libcst as cst
import libcst.matchers as m
from libcst.codemod._context import CodemodContext
from libcst.codemod._visitor import ContextAwareVisitor
from libcst.helpers import get_full_name_for_node


class GatherExportsVisitor(ContextAwareVisitor):
    """
    Gathers all explicit exports in a module and stores them as attributes on the
    instance. Intended to be instantiated and passed to a :class:`~libcst.Module`
    :meth:`~libcst.CSTNode.visit` method in order to gather up information about
    exports specified in an ``__all__`` variable inside a module.

    After visiting a module the following attributes will be populated:

     explicit_exported_objects
      A sequence of strings representing objects that the module exports
      directly. Note that when ``__all__`` is absent, this attribute does not
      store default exported objects by name.

    For more information on ``__all__``, please see Python's `Modules Documentation
    <https://docs.python.org/3/tutorial/modules.html>`_.
    """

    def __init__(self, context: CodemodContext) -> None:
        super().__init__(context)
        # Track any re-exported objects in an __all__ reference and whether
        # they're defined or not
        self.explicit_exported_objects: Set[str] = set()

        # Presumably at some point in the future it would be useful to grab
        # a list of all implicitly exported objects. That would go here as
        # well and would follow Python's rule for importing objects that
        # do not start with an underscore. Because of that, I named the above
        # `explicit_exported_objects` instead of just `exported_objects` so
        # that we have a reasonable place to put implicit objects in the future.

        # Internal bookkeeping
        self._is_assigned_export: Set[Union[cst.Tuple, cst.List, cst.Set]] = set()
        self._in_assigned_export: Set[Union[cst.Tuple, cst.List, cst.Set]] = set()

    def visit_AnnAssign(self, node: cst.AnnAssign) -> bool:
        value = node.value
        if value:
            if self._handle_assign_target(node.target, value):
                return True
        return False

    def visit_AugAssign(self, node: cst.AugAssign) -> bool:
        if m.matches(
            node,
            m.AugAssign(
                target=m.Name("__all__"),
                operator=m.AddAssign(),
                value=m.List() | m.Tuple(),
            ),
        ):
            value = node.value
            if isinstance(value, (cst.List, cst.Tuple)):
                self._is_assigned_export.add(value)
            return True
        return False

    def visit_Assign(self, node: cst.Assign) -> bool:
        for target_node in node.targets:
            if self._handle_assign_target(target_node.target, node.value):
                return True
        return False

    def _handle_assign_target(
        self, target: cst.BaseExpression, value: cst.BaseExpression
    ) -> bool:
        target_name = get_full_name_for_node(target)
        if target_name == "__all__":
            # Assignments such as `__all__ = ["os"]`
            # or `__all__ = exports = ["os"]`
            if isinstance(value, (cst.List, cst.Tuple, cst.Set)):
                self._is_assigned_export.add(value)
                return True
        elif isinstance(target, cst.Tuple) and isinstance(value, cst.Tuple):
            # Assignments such as `__all__, x = ["os"], []`
            for element_idx, element_node in enumerate(target.elements):
                element_name = get_full_name_for_node(element_node.value)
                if element_name == "__all__":
                    element_value = value.elements[element_idx].value
                    if isinstance(element_value, (cst.List, cst.Tuple, cst.Set)):
                        self._is_assigned_export.add(value)
                        self._is_assigned_export.add(element_value)
                        return True
        return False

    def visit_List(self, node: cst.List) -> bool:
        if node in self._is_assigned_export:
            self._in_assigned_export.add(node)
            return True
        return False

    def leave_List(self, original_node: cst.List) -> None:
        self._is_assigned_export.discard(original_node)
        self._in_assigned_export.discard(original_node)

    def visit_Tuple(self, node: cst.Tuple) -> bool:
        if node in self._is_assigned_export:
            self._in_assigned_export.add(node)
            return True
        return False

    def leave_Tuple(self, original_node: cst.Tuple) -> None:
        self._is_assigned_export.discard(original_node)
        self._in_assigned_export.discard(original_node)

    def visit_Set(self, node: cst.Set) -> bool:
        if node in self._is_assigned_export:
            self._in_assigned_export.add(node)
            return True
        return False

    def leave_Set(self, original_node: cst.Set) -> None:
        self._is_assigned_export.discard(original_node)
        self._in_assigned_export.discard(original_node)

    def visit_SimpleString(self, node: cst.SimpleString) -> bool:
        self._handle_string_export(node)
        return False

    def visit_ConcatenatedString(self, node: cst.ConcatenatedString) -> bool:
        self._handle_string_export(node)
        return False

    def _handle_string_export(
        self, node: Union[cst.SimpleString, cst.ConcatenatedString]
    ) -> None:
        if self._in_assigned_export:
            name = node.evaluated_value
            if not isinstance(name, str):
                return
            self.explicit_exported_objects.add(name)
