# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import inspect
from typing import (
    Callable,
    cast,
    Iterable,
    List,
    Mapping,
    MutableMapping,
    Optional,
    TYPE_CHECKING,
)

from libcst._metadata_dependent import MetadataDependent
from libcst._typed_visitor import CSTTypedVisitorFunctions
from libcst._visitors import CSTNodeT, CSTVisitor

if TYPE_CHECKING:
    from libcst._nodes.base import CSTNode  # noqa: F401

VisitorMethod = Callable[["CSTNode"], None]
_VisitorMethodCollection = Mapping[str, List[VisitorMethod]]


class BatchableCSTVisitor(CSTTypedVisitorFunctions, MetadataDependent):
    """
    The low-level base visitor class for traversing a CST as part of a batched
    set of traversals. This should be used in conjunction with the
    :func:`~libcst.visit_batched` function or the
    :func:`~libcst.MetadataWrapper.visit_batched` method from
    :class:`~libcst.MetadataWrapper` to visit a tree.
    Instances of this class cannot modify the tree.
    """

    def get_visitors(self) -> Mapping[str, VisitorMethod]:
        """
        Returns a mapping of all the ``visit_<Type[CSTNode]>``,
        ``visit_<Type[CSTNode]>_<attribute>``, ``leave_<Type[CSTNode]>`` and
        `leave_<Type[CSTNode]>_<attribute>`` methods defined by this visitor,
        excluding all empty stubs.
        """

        methods = inspect.getmembers(
            self,
            lambda m: (
                inspect.ismethod(m)
                and (m.__name__.startswith("visit_") or m.__name__.startswith("leave_"))
                and not getattr(m, "_is_no_op", False)
            ),
        )

        # TODO: verify all visitor methods reference valid node classes.
        # for name, __ in methods:
        #     ...

        return dict(methods)


def visit_batched(
    node: CSTNodeT,
    batchable_visitors: Iterable[BatchableCSTVisitor],
    before_visit: Optional[VisitorMethod] = None,
    after_leave: Optional[VisitorMethod] = None,
) -> CSTNodeT:
    """
    Do a batched traversal over ``node`` with all ``visitors``.

    ``before_visit`` and ``after_leave`` are provided as optional hooks to
    execute before the ``visit_<Type[CSTNode]>`` and after the
    ``leave_<Type[CSTNode]>`` methods from each visitor in ``visitor`` are
    executed by the batched visitor.

    This function does not handle metadata dependency resolution for ``visitors``.
    See :func:`~libcst.MetadataWrapper.visit_batched` from
    :class:`~libcst.MetadataWrapper` for batched traversal with metadata dependency
    resolution.
    """
    visitor_methods = _get_visitor_methods(batchable_visitors)
    batched_visitor = _BatchedCSTVisitor(
        visitor_methods, before_visit=before_visit, after_leave=after_leave
    )
    return cast(CSTNodeT, node.visit(batched_visitor))


def _get_visitor_methods(
    batchable_visitors: Iterable[BatchableCSTVisitor],
) -> _VisitorMethodCollection:
    """
    Gather all ``visit_<Type[CSTNode]>``, ``visit_<Type[CSTNode]>_<attribute>``,
    ``leave_<Type[CSTNode]>`` amd `leave_<Type[CSTNode]>_<attribute>`` methods
    from ``batchabled_visitors``.
    """
    visitor_methods: MutableMapping[str, List[VisitorMethod]] = {}
    for bv in batchable_visitors:
        for name, fn in bv.get_visitors().items():
            visitor_methods.setdefault(name, []).append(fn)
    return visitor_methods


class _BatchedCSTVisitor(CSTVisitor):
    """
    Internal visitor class to perform batched traversal over a tree.
    """

    visitor_methods: _VisitorMethodCollection
    before_visit: Optional[VisitorMethod]
    after_leave: Optional[VisitorMethod]

    def __init__(
        self,
        visitor_methods: _VisitorMethodCollection,
        *,
        before_visit: Optional[VisitorMethod] = None,
        after_leave: Optional[VisitorMethod] = None,
    ) -> None:
        super().__init__()
        self.visitor_methods = visitor_methods
        self.before_visit = before_visit
        self.after_leave = after_leave

    def on_visit(self, node: "CSTNode") -> bool:
        """
        Call appropriate visit methods on node before visiting children.
        """
        before_visit = self.before_visit
        if before_visit is not None:
            before_visit(node)
        type_name = type(node).__name__
        for v in self.visitor_methods.get(f"visit_{type_name}", []):
            v(node)
        return True

    def on_leave(self, original_node: "CSTNode") -> None:
        """
        Call appropriate leave methods on node after visiting children.
        """
        type_name = type(original_node).__name__
        for v in self.visitor_methods.get(f"leave_{type_name}", []):
            v(original_node)
        after_leave = self.after_leave
        if after_leave is not None:
            after_leave(original_node)

    def on_visit_attribute(self, node: "CSTNode", attribute: str) -> None:
        """
        Call appropriate visit attribute methods on node before visiting
        attribute's children.
        """
        type_name = type(node).__name__
        for v in self.visitor_methods.get(f"visit_{type_name}_{attribute}", []):
            v(node)

    def on_leave_attribute(self, original_node: "CSTNode", attribute: str) -> None:
        """
        Call appropriate leave attribute methods on node after visiting
        attribute's children.
        """
        type_name = type(original_node).__name__
        for v in self.visitor_methods.get(f"leave_{type_name}_{attribute}", []):
            v(original_node)
