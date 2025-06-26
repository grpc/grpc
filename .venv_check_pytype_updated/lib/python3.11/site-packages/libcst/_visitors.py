# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import TYPE_CHECKING, Union

from libcst._flatten_sentinel import FlattenSentinel
from libcst._metadata_dependent import MetadataDependent
from libcst._removal_sentinel import RemovalSentinel
from libcst._typed_visitor import CSTTypedTransformerFunctions, CSTTypedVisitorFunctions
from libcst._types import CSTNodeT

if TYPE_CHECKING:
    # Circular dependency for typing reasons only
    from libcst._nodes.base import CSTNode  # noqa: F401


CSTVisitorT = Union["CSTTransformer", "CSTVisitor"]


class CSTTransformer(CSTTypedTransformerFunctions, MetadataDependent):
    """
    The low-level base visitor class for traversing a CST and creating an
    updated copy of the original CST. This should be used in conjunction with
    the :func:`~libcst.CSTNode.visit` method on a :class:`~libcst.CSTNode` to
    visit each element in a tree starting with that node, and possibly returning
    a new node in its place.

    When visiting nodes using a :class:`CSTTransformer`, the return value of
    :func:`~libcst.CSTNode.visit` will be a new tree with any changes made in
    :func:`~libcst.CSTTransformer.on_leave` calls reflected in its children.
    """

    def on_visit(self, node: "CSTNode") -> bool:
        """
        Called every time a node is visited, before we've visited its children.

        Returns ``True`` if children should be visited, and returns ``False``
        otherwise.
        """
        visit_func = getattr(self, f"visit_{type(node).__name__}", None)
        if visit_func is not None:
            retval = visit_func(node)
        else:
            retval = True
        # Don't visit children IFF the visit function returned False.
        return False if retval is False else True

    def on_leave(
        self, original_node: CSTNodeT, updated_node: CSTNodeT
    ) -> Union[CSTNodeT, RemovalSentinel, FlattenSentinel[CSTNodeT]]:
        """
        Called every time we leave a node, after we've visited its children. If
        the :func:`~libcst.CSTTransformer.on_visit` function for this node returns
        ``False``, this function will still be called on that node.

        ``original_node`` is guaranteed to be the same node as is passed to
        :func:`~libcst.CSTTransformer.on_visit`, so it is safe to do state-based
        checks using the ``is`` operator. Modifications should always be performed
        on the ``updated_node`` so as to not overwrite changes made by child
        visits.

        Returning :attr:`RemovalSentinel.REMOVE` indicates that the node should be
        removed from its parent. This is not always possible, and may raise an
        exception if this node is required. As a convenience, you can use
        :func:`RemoveFromParent` as an alias to :attr:`RemovalSentinel.REMOVE`.
        """
        leave_func = getattr(self, f"leave_{type(original_node).__name__}", None)
        if leave_func is not None:
            updated_node = leave_func(original_node, updated_node)

        return updated_node

    def on_visit_attribute(self, node: "CSTNode", attribute: str) -> None:
        """
        Called before a node's child attribute is visited and after we have called
        :func:`~libcst.CSTTransformer.on_visit` on the node. A node's child
        attributes are visited in the order that they appear in source that this
        node originates from.
        """
        visit_func = getattr(self, f"visit_{type(node).__name__}_{attribute}", None)
        if visit_func is not None:
            visit_func(node)

    def on_leave_attribute(self, original_node: "CSTNode", attribute: str) -> None:
        """
        Called after a node's child attribute is visited and before we have called
        :func:`~libcst.CSTTransformer.on_leave` on the node.

        Unlike :func:`~libcst.CSTTransformer.on_leave`, this function does
        not allow modifications to the tree and is provided solely for state
        management.
        """
        leave_func = getattr(
            self, f"leave_{type(original_node).__name__}_{attribute}", None
        )
        if leave_func is not None:
            leave_func(original_node)


class CSTVisitor(CSTTypedVisitorFunctions, MetadataDependent):
    """
    The low-level base visitor class for traversing a CST. This should be used in
    conjunction with the :func:`~libcst.CSTNode.visit` method on a
    :class:`~libcst.CSTNode` to visit each element in a tree starting with that
    node. Unlike :class:`CSTTransformer`, instances of this class cannot modify
    the tree.

    When visiting nodes using a :class:`CSTVisitor`, the return value of
    :func:`~libcst.CSTNode.visit` will equal the passed in tree.
    """

    def on_visit(self, node: "CSTNode") -> bool:
        """
        Called every time a node is visited, before we've visited its children.

        Returns ``True`` if children should be visited, and returns ``False``
        otherwise.
        """
        visit_func = getattr(self, f"visit_{type(node).__name__}", None)
        if visit_func is not None:
            retval = visit_func(node)
        else:
            retval = True
        # Don't visit children IFF the visit function returned False.
        return False if retval is False else True

    def on_leave(self, original_node: "CSTNode") -> None:
        """
        Called every time we leave a node, after we've visited its children. If
        the :func:`~libcst.CSTVisitor.on_visit` function for this node returns
        ``False``, this function will still be called on that node.
        """
        leave_func = getattr(self, f"leave_{type(original_node).__name__}", None)
        if leave_func is not None:
            leave_func(original_node)

    def on_visit_attribute(self, node: "CSTNode", attribute: str) -> None:
        """
        Called before a node's child attribute is visited and after we have called
        :func:`~libcst.CSTTransformer.on_visit` on the node. A node's child
        attributes are visited in the order that they appear in source that this
        node originates from.
        """
        visit_func = getattr(self, f"visit_{type(node).__name__}_{attribute}", None)
        if visit_func is not None:
            visit_func(node)

    def on_leave_attribute(self, original_node: "CSTNode", attribute: str) -> None:
        """
        Called after a node's child attribute is visited and before we have called
        :func:`~libcst.CSTVisitor.on_leave` on the node.
        """
        leave_func = getattr(
            self, f"leave_{type(original_node).__name__}_{attribute}", None
        )
        if leave_func is not None:
            leave_func(original_node)
