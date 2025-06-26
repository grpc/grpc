# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


from contextlib import contextmanager
from dataclasses import dataclass, field
from typing import Iterable, Iterator, List, Optional, Sequence, TYPE_CHECKING, Union

from libcst._add_slots import add_slots
from libcst._flatten_sentinel import FlattenSentinel
from libcst._maybe_sentinel import MaybeSentinel
from libcst._removal_sentinel import RemovalSentinel
from libcst._types import CSTNodeT

if TYPE_CHECKING:
    # These are circular dependencies only used for typing purposes
    from libcst._nodes.base import CSTNode  # noqa: F401
    from libcst._visitors import CSTVisitorT


@add_slots
@dataclass(frozen=False)
class CodegenState:
    # These are derived from a Module
    default_indent: str
    default_newline: str
    provider: object = None  # overridden by libcst.metadata.position_provider

    indent_tokens: List[str] = field(default_factory=list)
    tokens: List[str] = field(default_factory=list)

    def increase_indent(self, value: str) -> None:
        self.indent_tokens.append(value)

    def decrease_indent(self) -> None:
        self.indent_tokens.pop()

    def add_indent_tokens(self) -> None:
        self.tokens.extend(self.indent_tokens)

    def add_token(self, value: str) -> None:
        self.tokens.append(value)

    def before_codegen(self, node: "CSTNode") -> None:
        pass

    def after_codegen(self, node: "CSTNode") -> None:
        pass

    def pop_trailing_newline(self) -> None:
        """
        Called by :meth:`libcst.Module._codegen_impl` at the end of the file to remove
        the last token (a trailing newline), assuming the file isn't empty.
        """
        if len(self.tokens) > 0:
            # EmptyLine and all statements generate newlines, so we can be sure that the
            # last token (if we're not an empty file) is a newline.
            self.tokens.pop()

    @contextmanager
    def record_syntactic_position(
        self,
        node: "CSTNode",
        *,
        start_node: Optional["CSTNode"] = None,
        end_node: Optional["CSTNode"] = None,
    ) -> Iterator[None]:
        yield


def visit_required(
    parent: "CSTNode", fieldname: str, node: CSTNodeT, visitor: "CSTVisitorT"
) -> CSTNodeT:
    """
    Given a node, visits the node using `visitor`. If removal is attempted by the
    visitor, an exception is raised.
    """
    visitor.on_visit_attribute(parent, fieldname)
    result = node.visit(visitor)
    if isinstance(result, RemovalSentinel):
        raise TypeError(
            f"We got a RemovalSentinel while visiting a {type(node).__name__}. This "
            + "node's parent does not allow it to be removed."
        )
    elif isinstance(result, FlattenSentinel):
        raise TypeError(
            f"We got a FlattenSentinel while visiting a {type(node).__name__}. This "
            + "node's parent does not allow for it to be it to be replaced with a "
            + "sequence."
        )

    visitor.on_leave_attribute(parent, fieldname)
    return result


def visit_optional(
    parent: "CSTNode", fieldname: str, node: Optional[CSTNodeT], visitor: "CSTVisitorT"
) -> Optional[CSTNodeT]:
    """
    Given an optional node, visits the node if it exists with `visitor`. If the node is
    removed, returns None.
    """
    if node is None:
        visitor.on_visit_attribute(parent, fieldname)
        visitor.on_leave_attribute(parent, fieldname)
        return None
    visitor.on_visit_attribute(parent, fieldname)
    result = node.visit(visitor)
    if isinstance(result, FlattenSentinel):
        raise TypeError(
            f"We got a FlattenSentinel while visiting a {type(node).__name__}. This "
            + "node's parent does not allow for it to be it to be replaced with a "
            + "sequence."
        )
    visitor.on_leave_attribute(parent, fieldname)
    return None if isinstance(result, RemovalSentinel) else result


def visit_sentinel(
    parent: "CSTNode",
    fieldname: str,
    node: Union[CSTNodeT, MaybeSentinel],
    visitor: "CSTVisitorT",
) -> Union[CSTNodeT, MaybeSentinel]:
    """
    Given a node that can be a real value or a sentinel value, visits the node if it
    is real with `visitor`. If the node is removed, returns MaybeSentinel.
    """
    if isinstance(node, MaybeSentinel):
        visitor.on_visit_attribute(parent, fieldname)
        visitor.on_leave_attribute(parent, fieldname)
        return MaybeSentinel.DEFAULT
    visitor.on_visit_attribute(parent, fieldname)
    result = node.visit(visitor)
    if isinstance(result, FlattenSentinel):
        raise TypeError(
            f"We got a FlattenSentinel while visiting a {type(node).__name__}. This "
            + "node's parent does not allow for it to be it to be replaced with a "
            + "sequence."
        )
    visitor.on_leave_attribute(parent, fieldname)
    return MaybeSentinel.DEFAULT if isinstance(result, RemovalSentinel) else result


def visit_iterable(
    parent: "CSTNode",
    fieldname: str,
    children: Iterable[CSTNodeT],
    visitor: "CSTVisitorT",
) -> Iterable[CSTNodeT]:
    """
    Given an iterable of children, visits each child with `visitor`, and yields the new
    children with any `RemovalSentinel` values removed.
    """
    visitor.on_visit_attribute(parent, fieldname)
    for child in children:
        new_child = child.visit(visitor)
        if isinstance(new_child, FlattenSentinel):
            yield from new_child
        elif not isinstance(new_child, RemovalSentinel):
            yield new_child
    visitor.on_leave_attribute(parent, fieldname)


def visit_sequence(
    parent: "CSTNode",
    fieldname: str,
    children: Sequence[CSTNodeT],
    visitor: "CSTVisitorT",
) -> Sequence[CSTNodeT]:
    """
    A convenience wrapper for `visit_iterable` that returns a sequence instead of an
    iterable.
    """
    return tuple(visit_iterable(parent, fieldname, children, visitor))


def visit_body_iterable(
    parent: "CSTNode",
    fieldname: str,
    children: Sequence[CSTNodeT],
    visitor: "CSTVisitorT",
) -> Iterable[CSTNodeT]:
    """
    Similar to visit_iterable above, but capable of discarding empty SimpleStatementLine
    nodes in order to preserve correct pass insertion behavior.
    """

    visitor.on_visit_attribute(parent, fieldname)
    for child in children:
        new_child = child.visit(visitor)

        # Don't yield a child if we removed it.
        if isinstance(new_child, RemovalSentinel):
            continue

        # Don't yield a child if the old child wasn't empty
        # and the new child is. This means a RemovalSentinel
        # caused a child of this node to be dropped, and it
        # is now useless.

        if isinstance(new_child, FlattenSentinel):
            for child_ in new_child:
                if (not child._is_removable()) and child_._is_removable():
                    continue
                yield child_
        else:
            if (not child._is_removable()) and new_child._is_removable():
                continue
            # Safe to yield child in this case.
            yield new_child
    visitor.on_leave_attribute(parent, fieldname)


def visit_body_sequence(
    parent: "CSTNode",
    fieldname: str,
    children: Sequence[CSTNodeT],
    visitor: "CSTVisitorT",
) -> Sequence[CSTNodeT]:
    """
    A convenience wrapper for `visit_body_iterable` that returns a sequence
    instead of an iterable.
    """
    return tuple(visit_body_iterable(parent, fieldname, children, visitor))
