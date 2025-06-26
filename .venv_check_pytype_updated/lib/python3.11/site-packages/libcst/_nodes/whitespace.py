# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


import re
from abc import ABC, abstractmethod
from dataclasses import dataclass
from typing import Optional, Pattern, Sequence

from libcst._add_slots import add_slots
from libcst._nodes.base import BaseLeaf, BaseValueToken, CSTNode, CSTValidationError
from libcst._nodes.internal import (
    CodegenState,
    visit_optional,
    visit_required,
    visit_sequence,
)
from libcst._visitors import CSTVisitorT

# SimpleWhitespace includes continuation characters, which must be followed immediately
# by a newline. SimpleWhitespace does not include other kinds of newlines, because those
# may have semantic significance.
SIMPLE_WHITESPACE_RE: Pattern[str] = re.compile(r"([ \f\t]|\\(\r\n?|\n))*", re.UNICODE)
NEWLINE_RE: Pattern[str] = re.compile(r"\r\n?|\n", re.UNICODE)
COMMENT_RE: Pattern[str] = re.compile(r"#[^\r\n]*", re.UNICODE)


class BaseParenthesizableWhitespace(CSTNode, ABC):
    """
    This is the kind of whitespace you might see inside the body of a statement or
    expression between two tokens. This is the most common type of whitespace.

    The list of allowed characters in a whitespace depends on whether it is found
    inside a parenthesized expression or not. This class allows nodes which can be
    found inside or outside a ``()``, ``[]`` or ``{}`` section to accept either
    whitespace form.

    https://docs.python.org/3/reference/lexical_analysis.html#implicit-line-joining

    Parenthesizable whitespace may contain a backslash character (``\\``), when used as
    a line-continuation character. While the continuation character isn't technically
    "whitespace", it serves the same purpose.

    Parenthesizable whitespace is often non-semantic (optional), but in cases where
    whitespace solves a grammar ambiguity between tokens (e.g. ``if test``, versus
    ``iftest``), it has some semantic value.
    """

    __slots__ = ()

    # TODO: Should we somehow differentiate places where we require non-zero whitespace
    # with a separate type?

    @property
    @abstractmethod
    def empty(self) -> bool:
        """
        Indicates that this node is empty (zero whitespace characters).
        """
        ...


@add_slots
@dataclass(frozen=True)
class SimpleWhitespace(BaseParenthesizableWhitespace, BaseValueToken):
    """
    This is the kind of whitespace you might see inside the body of a statement or
    expression between two tokens. This is the most common type of whitespace.

    A simple whitespace cannot contain a newline character unless it is directly
    preceeded by a line continuation character (``\\``). It can contain zero or
    more spaces or tabs. If you need a newline character without a line continuation
    character, use :class:`ParenthesizedWhitespace` instead.

    Simple whitespace is often non-semantic (optional), but in cases where whitespace
    solves a grammar ambiguity between tokens (e.g. ``if test``, versus ``iftest``),
    it has some semantic value.

    An example :class:`SimpleWhitespace` containing a space, a line continuation,
    a newline and another space is as follows::

        SimpleWhitespace(r" \\\\n ")
    """

    #: Actual string value of the simple whitespace. A legal value contains only
    #: space, ``\f`` and ``\t`` characters, and optionally a continuation
    #: (``\``) followed by a newline (``\n`` or ``\r\n``).
    value: str

    def _validate(self) -> None:
        if SIMPLE_WHITESPACE_RE.fullmatch(self.value) is None:
            raise CSTValidationError(
                f"Got non-whitespace value for whitespace node: {repr(self.value)}"
            )

    @property
    def empty(self) -> bool:
        """
        Indicates that this node is empty (zero whitespace characters).
        """

        return len(self.value) == 0


@add_slots
@dataclass(frozen=True)
class Newline(BaseLeaf):
    """
    Represents the newline that ends an :class:`EmptyLine` or a statement (as part of
    :class:`TrailingWhitespace`).

    Other newlines may occur in the document after continuation characters (the
    backslash, ``\\``), but those newlines are treated as part of the
    :class:`SimpleWhitespace`.

    Optionally, a value can be specified in order to overwrite the module's default
    newline. In general, this should be left as the default, which is ``None``. This
    is allowed because python modules are permitted to mix multiple unambiguous
    newline markers.
    """

    #: A value of ``None`` indicates that the module's default newline sequence should
    #: be used. A value of ``\n`` or ``\r\n`` indicates that the exact value specified
    #: will be used for this newline.
    value: Optional[str] = None

    def _validate(self) -> None:
        value = self.value
        if value and NEWLINE_RE.fullmatch(value) is None:
            raise CSTValidationError(
                f"Got an invalid value for newline node: {repr(value)}"
            )

    def _codegen_impl(self, state: CodegenState) -> None:
        value = self.value
        state.add_token(state.default_newline if value is None else value)


@add_slots
@dataclass(frozen=True)
class Comment(BaseValueToken):
    """
    A comment including the leading pound (``#``) character.

    The leading pound character is included in the 'value' property (instead of being
    stripped) to help re-enforce the idea that whitespace immediately after the pound
    character may be significant. E.g::

        # comment with whitespace at the start (usually preferred)
        #comment without whitespace at the start (usually not desirable)

    Usually wrapped in a :class:`TrailingWhitespace` or :class:`EmptyLine` node.
    """

    #: The comment itself. Valid values start with the pound (``#``) character followed
    #: by zero or more non-newline characters. Comments cannot include newlines.
    value: str

    def _validate(self) -> None:
        if COMMENT_RE.fullmatch(self.value) is None:
            raise CSTValidationError(
                f"Got non-comment value for comment node: {repr(self.value)}"
            )


@add_slots
@dataclass(frozen=True)
class TrailingWhitespace(CSTNode):
    """
    The whitespace at the end of a line after a statement. If a line contains only
    whitespace, :class:`EmptyLine` should be used instead.
    """

    #: Any simple whitespace before any comment or newline.
    whitespace: SimpleWhitespace = SimpleWhitespace.field("")

    #: An optional comment appearing after any simple whitespace.
    comment: Optional[Comment] = None

    #: The newline character that terminates this trailing whitespace.
    newline: Newline = Newline.field()

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "TrailingWhitespace":
        return TrailingWhitespace(
            whitespace=visit_required(self, "whitespace", self.whitespace, visitor),
            comment=visit_optional(self, "comment", self.comment, visitor),
            newline=visit_required(self, "newline", self.newline, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        self.whitespace._codegen(state)
        comment = self.comment
        if comment is not None:
            comment._codegen(state)
        self.newline._codegen(state)


@add_slots
@dataclass(frozen=True)
class EmptyLine(CSTNode):
    """
    Represents a line with only whitespace/comments. Usually statements will own any
    :class:`EmptyLine` nodes above themselves, and a :class:`Module` will own the
    document's header/footer :class:`EmptyLine` nodes.
    """

    #: An empty line doesn't have to correspond to the current indentation level. For
    #: example, this happens when all trailing whitespace is stripped and there is
    #: an empty line between two statements.
    indent: bool = True

    #: Extra whitespace after the indent, but before the comment.
    whitespace: SimpleWhitespace = SimpleWhitespace.field("")

    #: An optional comment appearing after the indent and extra whitespace.
    comment: Optional[Comment] = None

    #: The newline character that terminates this empty line.
    newline: Newline = Newline.field()

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "EmptyLine":
        return EmptyLine(
            indent=self.indent,
            whitespace=visit_required(self, "whitespace", self.whitespace, visitor),
            comment=visit_optional(self, "comment", self.comment, visitor),
            newline=visit_required(self, "newline", self.newline, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        if self.indent:
            state.add_indent_tokens()
        self.whitespace._codegen(state)
        comment = self.comment
        if comment is not None:
            comment._codegen(state)
        self.newline._codegen(state)


@add_slots
@dataclass(frozen=True)
class ParenthesizedWhitespace(BaseParenthesizableWhitespace):
    """
    This is the kind of whitespace you might see inside a parenthesized expression
    or statement between two tokens when there is a newline without a line
    continuation (``\\``) character.

    https://docs.python.org/3/reference/lexical_analysis.html#implicit-line-joining

    A parenthesized whitespace cannot be empty since it requires at least one
    :class:`TrailingWhitespace`. If you have whitespace that does not contain
    comments or newlines, use :class:`SimpleWhitespace` instead.
    """

    #: The whitespace that comes after the previous node, up to and including
    #: the end-of-line comment and newline.
    first_line: TrailingWhitespace = TrailingWhitespace.field()

    #: Any lines after the first that contain only indentation and/or comments.
    empty_lines: Sequence[EmptyLine] = ()

    #: Whether or not the final simple whitespace is indented regularly.
    indent: bool = False

    #: Extra whitespace after the indent, but before the next node.
    last_line: SimpleWhitespace = SimpleWhitespace.field("")

    def _visit_and_replace_children(
        self, visitor: CSTVisitorT
    ) -> "ParenthesizedWhitespace":
        return ParenthesizedWhitespace(
            first_line=visit_required(self, "first_line", self.first_line, visitor),
            empty_lines=visit_sequence(self, "empty_lines", self.empty_lines, visitor),
            indent=self.indent,
            last_line=visit_required(self, "last_line", self.last_line, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        self.first_line._codegen(state)
        for line in self.empty_lines:
            line._codegen(state)
        if self.indent:
            state.add_indent_tokens()
        self.last_line._codegen(state)

    @property
    def empty(self) -> bool:
        """
        Indicates that this node is empty (zero whitespace characters). For
        :class:`ParenthesizedWhitespace` this will always be ``False``.
        """

        # Its not possible to have a ParenthesizedWhitespace with zero characers.
        # If we did, the TrailingWhitespace would not have parsed.
        return False
