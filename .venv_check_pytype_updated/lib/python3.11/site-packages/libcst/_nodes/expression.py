# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


import re
from abc import ABC, abstractmethod
from ast import literal_eval
from contextlib import contextmanager
from dataclasses import dataclass, field
from enum import auto, Enum
from tokenize import (
    Floatnumber as FLOATNUMBER_RE,
    Imagnumber as IMAGNUMBER_RE,
    Intnumber as INTNUMBER_RE,
)
from typing import Callable, Generator, Literal, Optional, Sequence, Union

from libcst import CSTLogicError

from libcst._add_slots import add_slots
from libcst._maybe_sentinel import MaybeSentinel
from libcst._nodes.base import CSTCodegenError, CSTNode, CSTValidationError
from libcst._nodes.internal import (
    CodegenState,
    visit_optional,
    visit_required,
    visit_sentinel,
    visit_sequence,
)
from libcst._nodes.op import (
    AssignEqual,
    BaseBinaryOp,
    BaseBooleanOp,
    BaseCompOp,
    BaseUnaryOp,
    Colon,
    Comma,
    Dot,
    In,
    Is,
    IsNot,
    Not,
    NotIn,
)
from libcst._nodes.whitespace import BaseParenthesizableWhitespace, SimpleWhitespace
from libcst._visitors import CSTVisitorT


@add_slots
@dataclass(frozen=True)
class LeftSquareBracket(CSTNode):
    """
    Used by various nodes to denote a subscript or list section. This doesn't own
    the whitespace to the left of it since this is owned by the parent node.
    """

    #: Any space that appears directly after this left square bracket.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "LeftSquareBracket":
        return LeftSquareBracket(
            whitespace_after=visit_required(
                self, "whitespace_after", self.whitespace_after, visitor
            )
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        state.add_token("[")
        self.whitespace_after._codegen(state)


@add_slots
@dataclass(frozen=True)
class RightSquareBracket(CSTNode):
    """
    Used by various nodes to denote a subscript or list section. This doesn't own
    the whitespace to the right of it since this is owned by the parent node.
    """

    #: Any space that appears directly before this right square bracket.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "RightSquareBracket":
        return RightSquareBracket(
            whitespace_before=visit_required(
                self, "whitespace_before", self.whitespace_before, visitor
            )
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        self.whitespace_before._codegen(state)
        state.add_token("]")


@add_slots
@dataclass(frozen=True)
class LeftCurlyBrace(CSTNode):
    """
    Used by various nodes to denote a dict or set. This doesn't own the whitespace to
    the left of it since this is owned by the parent node.
    """

    #: Any space that appears directly after this left curly brace.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "LeftCurlyBrace":
        return LeftCurlyBrace(
            whitespace_after=visit_required(
                self, "whitespace_after", self.whitespace_after, visitor
            )
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        state.add_token("{")
        self.whitespace_after._codegen(state)


@add_slots
@dataclass(frozen=True)
class RightCurlyBrace(CSTNode):
    """
    Used by various nodes to denote a dict or set. This doesn't own the whitespace to
    the right of it since this is owned by the parent node.
    """

    #: Any space that appears directly before this right curly brace.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "RightCurlyBrace":
        return RightCurlyBrace(
            whitespace_before=visit_required(
                self, "whitespace_before", self.whitespace_before, visitor
            )
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        self.whitespace_before._codegen(state)
        state.add_token("}")


@add_slots
@dataclass(frozen=True)
class LeftParen(CSTNode):
    """
    Used by various nodes to denote a parenthesized section. This doesn't own
    the whitespace to the left of it since this is owned by the parent node.
    """

    #: Any space that appears directly after this left parenthesis.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "LeftParen":
        return LeftParen(
            whitespace_after=visit_required(
                self, "whitespace_after", self.whitespace_after, visitor
            )
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        state.add_token("(")
        self.whitespace_after._codegen(state)


@add_slots
@dataclass(frozen=True)
class RightParen(CSTNode):
    """
    Used by various nodes to denote a parenthesized section. This doesn't own
    the whitespace to the right of it since this is owned by the parent node.
    """

    #: Any space that appears directly after this left parenthesis.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "RightParen":
        return RightParen(
            whitespace_before=visit_required(
                self, "whitespace_before", self.whitespace_before, visitor
            )
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        self.whitespace_before._codegen(state)
        state.add_token(")")


@add_slots
@dataclass(frozen=True)
class Asynchronous(CSTNode):
    """
    Used by asynchronous function definitions, as well as ``async for`` and
    ``async with``.
    """

    #: Any space that appears directly after this async keyword.
    whitespace_after: SimpleWhitespace = SimpleWhitespace.field(" ")

    def _validate(self) -> None:
        if len(self.whitespace_after.value) < 1:
            raise CSTValidationError("Must have at least one space after Asynchronous.")

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Asynchronous":
        return Asynchronous(
            whitespace_after=visit_required(
                self, "whitespace_after", self.whitespace_after, visitor
            )
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        with state.record_syntactic_position(self):
            state.add_token("async")
        self.whitespace_after._codegen(state)


class _BaseParenthesizedNode(CSTNode, ABC):
    """
    We don't want to have another level of indirection for parenthesis in
    our tree, since that makes us more of a CST than an AST. So, all the
    expressions or atoms that can be wrapped in parenthesis will subclass
    this to get that functionality.
    """

    __slots__ = ()

    lpar: Sequence[LeftParen] = ()
    # Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    def _validate(self) -> None:
        if self.lpar and not self.rpar:
            raise CSTValidationError("Cannot have left paren without right paren.")
        if not self.lpar and self.rpar:
            raise CSTValidationError("Cannot have right paren without left paren.")
        if len(self.lpar) != len(self.rpar):
            raise CSTValidationError("Cannot have unbalanced parens.")

    @contextmanager
    def _parenthesize(self, state: CodegenState) -> Generator[None, None, None]:
        for lpar in self.lpar:
            lpar._codegen(state)
        with state.record_syntactic_position(self):
            yield
        for rpar in self.rpar:
            rpar._codegen(state)


class ExpressionPosition(Enum):
    LEFT = auto()
    RIGHT = auto()


class BaseExpression(_BaseParenthesizedNode, ABC):
    """
    An base class for all expressions. :class:`BaseExpression` contains no fields.
    """

    __slots__ = ()

    def _safe_to_use_with_word_operator(self, position: ExpressionPosition) -> bool:
        """
        Returns true if this expression is safe to be use with a word operator
        such as "not" without space between the operator an ourselves. Examples
        where this is true are "not(True)", "(1)in[1,2,3]", etc. This base
        function handles parenthesized nodes, but certain nodes such as tuples,
        dictionaries and lists will override this to signifiy that they're always
        safe.
        """

        return len(self.lpar) > 0 and len(self.rpar) > 0

    def _check_left_right_word_concatenation_safety(
        self,
        position: ExpressionPosition,
        left: "BaseExpression",
        right: "BaseExpression",
    ) -> bool:
        if position == ExpressionPosition.RIGHT:
            return left._safe_to_use_with_word_operator(ExpressionPosition.RIGHT)
        if position == ExpressionPosition.LEFT:
            return right._safe_to_use_with_word_operator(ExpressionPosition.LEFT)
        return False


class BaseAssignTargetExpression(BaseExpression, ABC):
    """
    An expression that's valid on the left side of an assignment. That assignment may
    be part an :class:`Assign` node, or it may be part of a number of other control
    structures that perform an assignment, such as a :class:`For` loop.

    Python's grammar defines all expression as valid in this position, but the AST
    compiler further restricts the allowed types, which is what this type attempts to
    express.

    This is similar to a :class:`BaseDelTargetExpression`, but it also includes
    :class:`StarredElement` as a valid node.

    The set of valid nodes are defined as part of `CPython's AST context computation
    <https://github.com/python/cpython/blob/v3.8.0a4/Python/ast.c#L1120>`_.
    """

    __slots__ = ()


class BaseDelTargetExpression(BaseExpression, ABC):
    """
    An expression that's valid on the right side of a :class:`Del` statement.

    Python's grammar defines all expression as valid in this position, but the AST
    compiler further restricts the allowed types, which is what this type attempts to
    express.

    This is similar to a :class:`BaseAssignTargetExpression`, but it excludes
    :class:`StarredElement`.

    The set of valid nodes are defined as part of `CPython's AST context computation
    <https://github.com/python/cpython/blob/v3.8.0a4/Python/ast.c#L1120>`_ and as part
    of `CPython's bytecode compiler
    <https://github.com/python/cpython/blob/v3.8.0a4/Python/compile.c#L4854>`_.
    """

    __slots__ = ()


@add_slots
@dataclass(frozen=True)
class Name(BaseAssignTargetExpression, BaseDelTargetExpression):
    """
    A simple variable name. Names are typically used in the context of a variable
    access, an assignment, or a deletion.

    Dotted variable names (``a.b.c``) are represented with :class:`Attribute` nodes,
    and subscripted variable names (``a[b]``) are represented with :class:`Subscript`
    nodes.
    """

    #: The variable's name (or "identifier") as a string.
    value: str

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Name":
        return Name(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            value=self.value,
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _validate(self) -> None:
        super(Name, self)._validate()
        if len(self.value) == 0:
            raise CSTValidationError("Cannot have empty name identifier.")
        if not self.value.isidentifier():
            raise CSTValidationError(f"Name {self.value!r} is not a valid identifier.")

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state):
            state.add_token(self.value)


@add_slots
@dataclass(frozen=True)
class Ellipsis(BaseExpression):
    """
    An ellipsis ``...``. When used as an expression, it evaluates to the
    `Ellipsis constant`_. Ellipsis are often used as placeholders in code or in
    conjunction with :class:`SubscriptElement`.

    .. _Ellipsis constant: https://docs.python.org/3/library/constants.html#Ellipsis
    """

    lpar: Sequence[LeftParen] = ()

    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Ellipsis":
        return Ellipsis(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _safe_to_use_with_word_operator(self, position: ExpressionPosition) -> bool:
        return True

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state):
            state.add_token("...")


class BaseNumber(BaseExpression, ABC):
    """
    A type such as :class:`Integer`, :class:`Float`, or :class:`Imaginary` that can be
    used anywhere that you need to explicitly take any number type.
    """

    __slots__ = ()

    def _safe_to_use_with_word_operator(self, position: ExpressionPosition) -> bool:
        """
        Numbers are funny. The expression "5in [1,2,3,4,5]" is a valid expression
        which evaluates to "True". So, encapsulate that here by allowing zero spacing
        with the left hand side of an expression with a comparison operator.
        """
        if position == ExpressionPosition.LEFT:
            return True
        return super(BaseNumber, self)._safe_to_use_with_word_operator(position)


@add_slots
@dataclass(frozen=True)
class Integer(BaseNumber):
    #: A string representation of the integer, such as ``"100000"`` or ``100_000``.
    #:
    #: To convert this string representation to an ``int``, use the calculated
    #: property :attr:`~Integer.evaluated_value`.
    value: str

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Integer":
        return Integer(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            value=self.value,
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _validate(self) -> None:
        super(Integer, self)._validate()
        if not re.fullmatch(INTNUMBER_RE, self.value):
            raise CSTValidationError("Number is not a valid integer.")

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state):
            state.add_token(self.value)

    @property
    def evaluated_value(self) -> int:
        """
        Return an :func:`ast.literal_eval` evaluated int of :py:attr:`value`.
        """
        return literal_eval(self.value)


@add_slots
@dataclass(frozen=True)
class Float(BaseNumber):
    #: A string representation of the floating point number, such as ``"0.05"``,
    #: ``".050"``, or ``"5e-2"``.
    #:
    #: To convert this string representation to an ``float``, use the calculated
    #: property :attr:`~Float.evaluated_value`.
    value: str

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Float":
        return Float(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            value=self.value,
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _validate(self) -> None:
        super(Float, self)._validate()
        if not re.fullmatch(FLOATNUMBER_RE, self.value):
            raise CSTValidationError("Number is not a valid float.")

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state):
            state.add_token(self.value)

    @property
    def evaluated_value(self) -> float:
        """
        Return an :func:`ast.literal_eval` evaluated float of :py:attr:`value`.
        """
        return literal_eval(self.value)


@add_slots
@dataclass(frozen=True)
class Imaginary(BaseNumber):
    #: A string representation of the imaginary (complex) number, such as ``"2j"``.
    #:
    #: To convert this string representation to an ``complex``, use the calculated
    #: property :attr:`~Imaginary.evaluated_value`.
    value: str

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Imaginary":
        return Imaginary(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            value=self.value,
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _validate(self) -> None:
        super(Imaginary, self)._validate()
        if not re.fullmatch(IMAGNUMBER_RE, self.value):
            raise CSTValidationError("Number is not a valid imaginary.")

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state):
            state.add_token(self.value)

    @property
    def evaluated_value(self) -> complex:
        """
        Return an :func:`ast.literal_eval` evaluated complex of :py:attr:`value`.
        """
        return literal_eval(self.value)


class BaseString(BaseExpression, ABC):
    """
    A type that can be used anywhere that you need to take any string. This includes
    :class:`SimpleString`, :class:`ConcatenatedString`, and :class:`FormattedString`.
    """

    __slots__ = ()


StringQuoteLiteral = Literal['"', "'", '"""', "'''"]


class _BasePrefixedString(BaseString, ABC):
    __slots__ = ()

    @property
    def prefix(self) -> str:
        """
        Returns the string's prefix, if any exists.

        See `String and Bytes literals
        <https://docs.python.org/3.7/reference/lexical_analysis.html#string-and-bytes-literals>`_
        for more information.
        """
        ...

    @property
    def quote(self) -> StringQuoteLiteral:
        """
        Returns the quotation used to denote the string. Can be either ``'``,
        ``"``, ``'''`` or ``\"\"\"``.
        """
        ...

    def _safe_to_use_with_word_operator(self, position: ExpressionPosition) -> bool:
        """
        ``"a"in"abc`` is okay, but if you add a prefix, (e.g. ``b"a"inb"abc"``), the string
        is no longer valid on the RHS of the word operator, because it's not clear where
        the keyword ends and the prefix begins, unless it's parenthesized.
        """
        if position == ExpressionPosition.LEFT:
            return True
        elif self.prefix == "":  # and position == ExpressionPosition.RIGHT
            return True
        else:
            return super(_BasePrefixedString, self)._safe_to_use_with_word_operator(
                position
            )


@add_slots
@dataclass(frozen=True)
class SimpleString(_BasePrefixedString):
    """
    Any sort of literal string expression that is not a :class:`FormattedString`
    (f-string), including triple-quoted multi-line strings.
    """

    #: The texual representation of the string, including quotes, prefix characters, and
    #: any escape characters present in the original source code , such as
    #: ``r"my string\n"``. To remove the quotes and interpret any escape characters,
    #: use the calculated property :attr:`~SimpleString.evaluated_value`.
    value: str

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precidence dictation.
    rpar: Sequence[RightParen] = ()

    def _validate(self) -> None:
        super(SimpleString, self)._validate()

        # Validate any prefix
        prefix = self.prefix
        if prefix not in ("", "r", "u", "b", "br", "rb"):
            raise CSTValidationError("Invalid string prefix.")
        prefixlen = len(prefix)
        # Validate wrapping quotes
        if len(self.value) < (prefixlen + 2):
            raise CSTValidationError("String must have enclosing quotes.")
        if (
            self.value[prefixlen] not in ['"', "'"]
            or self.value[prefixlen] != self.value[-1]
        ):
            raise CSTValidationError("String must have matching enclosing quotes.")
        # Check validity of triple-quoted strings
        if len(self.value) >= (prefixlen + 6):
            if self.value[prefixlen] == self.value[prefixlen + 1]:
                # We know this isn't an empty string, so there needs to be a third
                # identical enclosing token.
                if (
                    self.value[prefixlen] != self.value[prefixlen + 2]
                    or self.value[prefixlen] != self.value[-2]
                    or self.value[prefixlen] != self.value[-3]
                ):
                    raise CSTValidationError(
                        "String must have matching enclosing quotes."
                    )
        # We should check the contents as well, but this is pretty complicated,
        # partially due to triple-quoted strings.

    @property
    def prefix(self) -> str:
        """
        Returns the string's prefix, if any exists. The prefix can be ``r``,
        ``u``, ``b``, ``br`` or ``rb``.
        """

        prefix: str = ""
        for c in self.value:
            if c in ['"', "'"]:
                break
            prefix += c
        return prefix.lower()

    @property
    def quote(self) -> StringQuoteLiteral:
        """
        Returns the quotation used to denote the string. Can be either ``'``,
        ``"``, ``'''`` or ``\"\"\"``.
        """

        quote: str = ""
        for char in self.value[len(self.prefix) :]:
            if char not in {"'", '"'}:
                break
            if quote and char != quote[0]:
                # This is no longer the same string quote
                break
            quote += char

        if len(quote) == 2:
            # Let's assume this is an empty string.
            quote = quote[:1]
        elif 3 < len(quote) <= 6:
            # Let's assume this can be one of the following:
            # >>> """"foo"""
            # '"foo'
            # >>> """""bar"""
            # '""bar'
            # >>> """"""
            # ''
            quote = quote[:3]

        if len(quote) not in {1, 3}:
            # We shouldn't get here due to construction validation logic,
            # but handle the case anyway.
            raise CSTLogicError(f"Invalid string {self.value}")

        # pyre-ignore We know via the above validation that we will only
        # ever return one of the four string literals.
        return quote

    @property
    def raw_value(self) -> str:
        """
        Returns the raw value of the string as it appears in source, without
        the beginning or end quotes and without the prefix. This is often
        useful when constructing transforms which need to manipulate strings
        in source code.
        """

        prefix_len = len(self.prefix)
        quote_len = len(self.quote)
        return self.value[(prefix_len + quote_len) : (-quote_len)]

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "SimpleString":
        return SimpleString(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            value=self.value,
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state):
            state.add_token(self.value)

    @property
    def evaluated_value(self) -> Union[str, bytes]:
        """
        Return an :func:`ast.literal_eval` evaluated str of :py:attr:`value`.
        """
        return literal_eval(self.value)


class BaseFormattedStringContent(CSTNode, ABC):
    """
    The base type for :class:`FormattedStringText` and
    :class:`FormattedStringExpression`. A :class:`FormattedString` is composed of a
    sequence of :class:`BaseFormattedStringContent` parts.
    """

    __slots__ = ()


@add_slots
@dataclass(frozen=True)
class FormattedStringText(BaseFormattedStringContent):
    """
    Part of a :class:`FormattedString` that is not inside curly braces (``{`` or ``}``).
    For example, in::

        f"ab{cd}ef"

    ``ab`` and ``ef`` are :class:`FormattedStringText` nodes, but ``{cd}`` is a
    :class:`FormattedStringExpression`.
    """

    #: The raw string value, including any escape characters present in the source
    #: code, not including any enclosing quotes.
    value: str

    def _visit_and_replace_children(
        self, visitor: CSTVisitorT
    ) -> "FormattedStringText":
        return FormattedStringText(value=self.value)

    def _codegen_impl(self, state: CodegenState) -> None:
        state.add_token(self.value)


@add_slots
@dataclass(frozen=True)
class FormattedStringExpression(BaseFormattedStringContent):
    """
    Part of a :class:`FormattedString` that is inside curly braces (``{`` or ``}``),
    including the surrounding curly braces. For example, in::

        f"ab{cd}ef"

    ``{cd}`` is a :class:`FormattedStringExpression`, but ``ab`` and ``ef`` are
    :class:`FormattedStringText` nodes.

    An f-string expression may contain ``conversion`` and ``format_spec`` suffixes that
    control how the expression is converted to a string. See `Python's language
    reference
    <https://docs.python.org/3/reference/lexical_analysis.html#formatted-string-literals>`__
    for details.
    """

    #: The expression we will evaluate and render when generating the string.
    expression: BaseExpression

    #: An optional conversion specifier, such as ``!s``, ``!r`` or ``!a``.
    conversion: Optional[str] = None

    #: An optional format specifier following the `format specification mini-language
    #: <https://docs.python.org/3/library/string.html#formatspec>`_.
    format_spec: Optional[Sequence[BaseFormattedStringContent]] = None

    #: Whitespace after the opening curly brace (``{``), but before the ``expression``.
    whitespace_before_expression: BaseParenthesizableWhitespace = (
        SimpleWhitespace.field("")
    )

    #: Whitespace after the ``expression``, but before the ``conversion``,
    #: ``format_spec`` and the closing curly brace (``}``). Python does not
    #: allow whitespace inside or after a ``conversion`` or ``format_spec``.
    whitespace_after_expression: BaseParenthesizableWhitespace = SimpleWhitespace.field(
        ""
    )

    #: Equal sign for formatted string expression uses self-documenting expressions,
    #: such as ``f"{x=}"``. See the `Python 3.8 release notes
    #: <https://docs.python.org/3/whatsnew/3.8.html#f-strings-support-for-self-documenting-expressions-and-debugging>`_.
    equal: Optional[AssignEqual] = None

    def _validate(self) -> None:
        if self.conversion is not None and self.conversion not in ("s", "r", "a"):
            raise CSTValidationError("Invalid f-string conversion.")

    def _visit_and_replace_children(
        self, visitor: CSTVisitorT
    ) -> "FormattedStringExpression":
        format_spec = self.format_spec
        return FormattedStringExpression(
            whitespace_before_expression=visit_required(
                self,
                "whitespace_before_expression",
                self.whitespace_before_expression,
                visitor,
            ),
            expression=visit_required(self, "expression", self.expression, visitor),
            equal=visit_optional(self, "equal", self.equal, visitor),
            whitespace_after_expression=visit_required(
                self,
                "whitespace_after_expression",
                self.whitespace_after_expression,
                visitor,
            ),
            conversion=self.conversion,
            format_spec=(
                visit_sequence(self, "format_spec", format_spec, visitor)
                if format_spec is not None
                else None
            ),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        state.add_token("{")
        self.whitespace_before_expression._codegen(state)
        self.expression._codegen(state)
        equal = self.equal
        if equal is not None:
            equal._codegen(state)
        self.whitespace_after_expression._codegen(state)
        conversion = self.conversion
        if conversion is not None:
            state.add_token("!")
            state.add_token(conversion)
        format_spec = self.format_spec
        if format_spec is not None:
            state.add_token(":")
            for spec in format_spec:
                spec._codegen(state)
        state.add_token("}")


@add_slots
@dataclass(frozen=True)
class FormattedString(_BasePrefixedString):
    """
    An "f-string". These formatted strings are string literals prefixed by the letter
    "f". An f-string may contain interpolated expressions inside curly braces (``{`` and
    ``}``).

    F-strings are defined in `PEP 498`_ and documented in `Python's language
    reference
    <https://docs.python.org/3/reference/lexical_analysis.html#formatted-string-literals>`__.

    >>> import libcst as cst
    >>> cst.parse_expression('f"ab{cd}ef"')
    FormattedString(
        parts=[
            FormattedStringText(
                value='ab',
            ),
            FormattedStringExpression(
                expression=Name(
                    value='cd',
                    lpar=[],
                    rpar=[],
                ),
                conversion=None,
                format_spec=None,
                whitespace_before_expression=SimpleWhitespace(
                    value='',
                ),
                whitespace_after_expression=SimpleWhitespace(
                    value='',
                ),
            ),
            FormattedStringText(
                value='ef',
            ),
        ],
        start='f"',
        end='"',
        lpar=[],
        rpar=[],
    )

    .. _PEP 498: https://www.python.org/dev/peps/pep-0498/#specification
    """

    #: A formatted string is composed as a series of :class:`FormattedStringText` and
    #: :class:`FormattedStringExpression` parts.
    parts: Sequence[BaseFormattedStringContent]

    #: The string prefix and the leading quote, such as ``f"``, ``F'``, ``fr"``, or
    #: ``f"""``.
    start: str = 'f"'

    #: The trailing quote. This must match the type of quote used in ``start``.
    end: Literal['"', "'", '"""', "'''"] = '"'

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precidence dictation.
    rpar: Sequence[RightParen] = ()

    def _validate(self) -> None:
        super(FormattedString, self)._validate()

        # Validate any prefix
        prefix = self.prefix
        if prefix not in ("f", "fr", "rf"):
            raise CSTValidationError("Invalid f-string prefix.")

        # Validate wrapping quotes
        starttoken = self.start[len(prefix) :]
        if starttoken != self.end:
            raise CSTValidationError("f-string must have matching enclosing quotes.")

        # Validate valid wrapping quote usage
        if starttoken not in ('"', "'", '"""', "'''"):
            raise CSTValidationError("Invalid f-string enclosing quotes.")

    @property
    def prefix(self) -> str:
        """
        Returns the string's prefix, if any exists. The prefix can be ``f``,
        ``fr``, or ``rf``.
        """

        prefix = ""
        for c in self.start:
            if c in ['"', "'"]:
                break
            prefix += c
        return prefix.lower()

    @property
    def quote(self) -> StringQuoteLiteral:
        """
        Returns the quotation used to denote the string. Can be either ``'``,
        ``"``, ``'''`` or ``\"\"\"``.
        """

        return self.end

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "FormattedString":
        return FormattedString(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            start=self.start,
            parts=visit_sequence(self, "parts", self.parts, visitor),
            end=self.end,
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state):
            state.add_token(self.start)
            for part in self.parts:
                part._codegen(state)
            state.add_token(self.end)


@add_slots
@dataclass(frozen=True)
class ConcatenatedString(BaseString):
    """
    Represents an implicitly concatenated string, such as::

        "abc" "def" == "abcdef"

    .. warning::
       This is different from two strings joined in a :class:`BinaryOperation` with an
       :class:`Add` operator, and is `sometimes viewed as an antifeature of Python
       <https://lwn.net/Articles/551426/>`_.
    """

    #: String on the left of the concatenation.
    left: Union[SimpleString, FormattedString]

    #: String on the right of the concatenation.
    right: Union[SimpleString, FormattedString, "ConcatenatedString"]

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precidence dictation.
    rpar: Sequence[RightParen] = ()

    #: Whitespace between the ``left`` and ``right`` substrings.
    whitespace_between: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    def _safe_to_use_with_word_operator(self, position: ExpressionPosition) -> bool:
        if super(ConcatenatedString, self)._safe_to_use_with_word_operator(position):
            # if we have parenthesis, we're safe.
            return True
        return self._check_left_right_word_concatenation_safety(
            position, self.left, self.right
        )

    def _validate(self) -> None:
        super(ConcatenatedString, self)._validate()

        # Strings that are concatenated cannot have parens.
        if bool(self.left.lpar) or bool(self.left.rpar):
            raise CSTValidationError("Cannot concatenate parenthesized strings.")
        if bool(self.right.lpar) or bool(self.right.rpar):
            raise CSTValidationError("Cannot concatenate parenthesized strings.")

        # Cannot concatenate str and bytes
        leftbytes = "b" in self.left.prefix
        right = self.right
        if isinstance(right, ConcatenatedString):
            rightbytes = "b" in right.left.prefix
        elif isinstance(right, SimpleString):
            rightbytes = "b" in right.prefix
        elif isinstance(right, FormattedString):
            rightbytes = "b" in right.prefix
        else:
            raise CSTLogicError("Logic error!")
        if leftbytes != rightbytes:
            raise CSTValidationError("Cannot concatenate string and bytes.")

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "ConcatenatedString":
        return ConcatenatedString(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            left=visit_required(self, "left", self.left, visitor),
            whitespace_between=visit_required(
                self, "whitespace_between", self.whitespace_between, visitor
            ),
            right=visit_required(self, "right", self.right, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state):
            self.left._codegen(state)
            self.whitespace_between._codegen(state)
            self.right._codegen(state)

    @property
    def evaluated_value(self) -> Union[str, bytes, None]:
        """
        Return an :func:`ast.literal_eval` evaluated str of recursively concatenated :py:attr:`left` and :py:attr:`right`
        if and only if both :py:attr:`left` and :py:attr:`right` are composed by :class:`SimpleString` or :class:`ConcatenatedString`
        (:class:`FormattedString` cannot be evaluated).
        """
        left = self.left
        right = self.right
        if isinstance(left, FormattedString) or isinstance(right, FormattedString):
            return None
        left_val = left.evaluated_value
        right_val = right.evaluated_value
        if right_val is None:
            return None
        if isinstance(left_val, bytes) and isinstance(right_val, bytes):
            return left_val + right_val
        if isinstance(left_val, str) and isinstance(right_val, str):
            return left_val + right_val
        return None


@add_slots
@dataclass(frozen=True)
class ComparisonTarget(CSTNode):
    """
    A target for a :class:`Comparison`. Owns the comparison operator and the value to
    the right of the operator.
    """

    #: A comparison operator such as ``<``, ``>=``, ``==``, ``is``, or ``in``.
    operator: BaseCompOp

    #: The right hand side of the comparison operation.
    comparator: BaseExpression

    def _validate(self) -> None:
        # Validate operator spacing rules
        operator = self.operator
        if (
            isinstance(operator, (In, NotIn, Is, IsNot))
            and operator.whitespace_after.empty
            and not self.comparator._safe_to_use_with_word_operator(
                ExpressionPosition.RIGHT
            )
        ):
            raise CSTValidationError(
                "Must have at least one space around comparison operator."
            )

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "ComparisonTarget":
        return ComparisonTarget(
            operator=visit_required(self, "operator", self.operator, visitor),
            comparator=visit_required(self, "comparator", self.comparator, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        self.operator._codegen(state)
        self.comparator._codegen(state)


@add_slots
@dataclass(frozen=True)
class Comparison(BaseExpression):
    """
    A comparison between multiple values such as ``x < y``, ``x < y < z``, or
    ``x in [y, z]``. These comparisions typically result in boolean values.

    Unlike :class:`BinaryOperation` and :class:`BooleanOperation`, comparisons are not
    restricted to a left and right child. Instead they can contain an arbitrary number
    of :class:`ComparisonTarget` children.

    ``x < y < z`` is not equivalent to ``(x < y) < z`` or ``x < (y < z)``. Instead,
    it's roughly equivalent to ``x < y and y < z``.

    For more details, see `Python's documentation on comparisons
    <https://docs.python.org/3/reference/expressions.html#comparisons>`_.

    ::

        # x < y < z

        Comparison(
            Name("x"),
            [
                ComparisonTarget(LessThan(), Name("y")),
                ComparisonTarget(LessThan(), Name("z")),
            ],
        )
    """

    #: The first value in the full sequence of values to compare. This value will be
    #: compared against the first value in ``comparisions``.
    left: BaseExpression

    #: Pairs of :class:`BaseCompOp` operators and expression values to compare. These
    #: come after ``left``. Each value is compared against the value before and after
    #: itself in the sequence.
    comparisons: Sequence[ComparisonTarget]

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    def _safe_to_use_with_word_operator(self, position: ExpressionPosition) -> bool:
        if super(Comparison, self)._safe_to_use_with_word_operator(position):
            # we have parenthesis
            return True
        return self._check_left_right_word_concatenation_safety(
            position, self.left, self.comparisons[-1].comparator
        )

    def _validate(self) -> None:
        # Perform any validation on base type
        super(Comparison, self)._validate()

        if len(self.comparisons) == 0:
            raise CSTValidationError("Must have at least one ComparisonTarget.")

        # Validate operator spacing rules
        previous_comparator = self.left
        for target in self.comparisons:
            operator = target.operator
            if (
                isinstance(operator, (In, NotIn, Is, IsNot))
                and operator.whitespace_before.empty
                and not previous_comparator._safe_to_use_with_word_operator(
                    ExpressionPosition.LEFT
                )
            ):
                raise CSTValidationError(
                    "Must have at least one space around comparison operator."
                )
            previous_comparator = target.comparator

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Comparison":
        return Comparison(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            left=visit_required(self, "left", self.left, visitor),
            comparisons=visit_sequence(self, "comparisons", self.comparisons, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state):
            self.left._codegen(state)
            for comp in self.comparisons:
                comp._codegen(state)


@add_slots
@dataclass(frozen=True)
class UnaryOperation(BaseExpression):
    """
    Any generic unary expression, such as ``not x`` or ``-x``. :class:`UnaryOperation`
    nodes apply a :class:`BaseUnaryOp` to an expression.
    """

    #: The unary operator that applies some operation (e.g. negation) to the
    #: ``expression``.
    operator: BaseUnaryOp

    #: The expression that should be transformed (e.g. negated) by the operator to
    #: create a new value.
    expression: BaseExpression

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    def _validate(self) -> None:
        # Perform any validation on base type
        super(UnaryOperation, self)._validate()

        if (
            isinstance(self.operator, Not)
            and self.operator.whitespace_after.empty
            and not self.expression._safe_to_use_with_word_operator(
                ExpressionPosition.RIGHT
            )
        ):
            raise CSTValidationError("Must have at least one space after not operator.")

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "UnaryOperation":
        return UnaryOperation(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            operator=visit_required(self, "operator", self.operator, visitor),
            expression=visit_required(self, "expression", self.expression, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _safe_to_use_with_word_operator(self, position: ExpressionPosition) -> bool:
        """
        As long as we aren't comprised of the Not unary operator, we are safe to use
        without space.
        """
        if super(UnaryOperation, self)._safe_to_use_with_word_operator(position):
            return True
        if position == ExpressionPosition.RIGHT:
            return not isinstance(self.operator, Not)
        if position == ExpressionPosition.LEFT:
            return self.expression._safe_to_use_with_word_operator(
                ExpressionPosition.LEFT
            )
        return False

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state):
            self.operator._codegen(state)
            self.expression._codegen(state)


@add_slots
@dataclass(frozen=True)
class BinaryOperation(BaseExpression):
    """
    An operation that combines two expression such as ``x << y`` or ``y + z``.
    :class:`BinaryOperation` nodes apply a :class:`BaseBinaryOp` to an expression.

    Binary operations do not include operations performed with :class:`BaseBooleanOp`
    nodes, such as ``and`` or ``or``. Instead, those operations are provided by
    :class:`BooleanOperation`.

    It also does not include support for comparision operators performed with
    :class:`BaseCompOp`, such as ``<``, ``>=``, ``==``, ``is``, or ``in``. Instead,
    those operations are provided by :class:`Comparison`.
    """

    #: The left hand side of the operation.
    left: BaseExpression

    #: The actual operator such as ``<<`` or ``+`` that combines the ``left`` and
    #: ``right`` expressions.
    operator: BaseBinaryOp

    #: The right hand side of the operation.
    right: BaseExpression

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "BinaryOperation":
        return BinaryOperation(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            left=visit_required(self, "left", self.left, visitor),
            operator=visit_required(self, "operator", self.operator, visitor),
            right=visit_required(self, "right", self.right, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _safe_to_use_with_word_operator(self, position: ExpressionPosition) -> bool:
        if super(BinaryOperation, self)._safe_to_use_with_word_operator(position):
            return True
        return self._check_left_right_word_concatenation_safety(
            position, self.left, self.right
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state):
            self.left._codegen(state)
            self.operator._codegen(state)
            self.right._codegen(state)


@add_slots
@dataclass(frozen=True)
class BooleanOperation(BaseExpression):
    """
    An operation that combines two booleans such as ``x or y`` or ``z and w``
    :class:`BooleanOperation` nodes apply a :class:`BaseBooleanOp` to an expression.

    Boolean operations do not include operations performed with :class:`BaseBinaryOp`
    nodes, such as ``+`` or ``<<``. Instead, those operations are provided by
    :class:`BinaryOperation`.

    It also does not include support for comparision operators performed with
    :class:`BaseCompOp`, such as ``<``, ``>=``, ``==``, ``is``, or ``in``. Instead,
    those operations are provided by :class:`Comparison`.
    """

    #: The left hand side of the operation.
    left: BaseExpression

    #: The actual operator such as ``and`` or ``or`` that combines the ``left`` and
    #: ``right`` expressions.
    operator: BaseBooleanOp

    #: The right hand side of the operation.
    right: BaseExpression

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    def _validate(self) -> None:
        # Paren validation and such
        super(BooleanOperation, self)._validate()
        # Validate spacing rules
        if (
            self.operator.whitespace_before.empty
            and not self.left._safe_to_use_with_word_operator(ExpressionPosition.LEFT)
        ):
            raise CSTValidationError(
                "Must have at least one space around boolean operator."
            )
        if (
            self.operator.whitespace_after.empty
            and not self.right._safe_to_use_with_word_operator(ExpressionPosition.RIGHT)
        ):
            raise CSTValidationError(
                "Must have at least one space around boolean operator."
            )

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "BooleanOperation":
        return BooleanOperation(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            left=visit_required(self, "left", self.left, visitor),
            operator=visit_required(self, "operator", self.operator, visitor),
            right=visit_required(self, "right", self.right, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _safe_to_use_with_word_operator(self, position: ExpressionPosition) -> bool:
        if super(BooleanOperation, self)._safe_to_use_with_word_operator(position):
            return True
        return self._check_left_right_word_concatenation_safety(
            position, self.left, self.right
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state):
            self.left._codegen(state)
            self.operator._codegen(state)
            self.right._codegen(state)


@add_slots
@dataclass(frozen=True)
class Attribute(BaseAssignTargetExpression, BaseDelTargetExpression):
    """
    An attribute reference, such as ``x.y``.

    Note that in the case of ``x.y.z``, the outer attribute will have an attr of ``z``
    and the value will be another :class:`Attribute` referencing the ``y`` attribute on
    ``x``::

        Attribute(
            value=Attribute(
                value=Name("x")
                attr=Name("y")
            ),
            attr=Name("z"),
        )
    """

    #: An expression which, when evaluated, will produce an object with ``attr`` as an
    #: attribute.
    value: BaseExpression

    #: The name of the attribute being accessed on the ``value`` object.
    attr: Name

    #: A separating dot. If there's whitespace between the ``value`` and ``attr``, this
    #: dot owns it.
    dot: Dot = Dot()

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Attribute":
        return Attribute(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            value=visit_required(self, "value", self.value, visitor),
            dot=visit_required(self, "dot", self.dot, visitor),
            attr=visit_required(self, "attr", self.attr, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _safe_to_use_with_word_operator(self, position: ExpressionPosition) -> bool:
        if super(Attribute, self)._safe_to_use_with_word_operator(position):
            return True
        return self._check_left_right_word_concatenation_safety(
            position, self.value, self.attr
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state):
            self.value._codegen(state)
            self.dot._codegen(state)
            self.attr._codegen(state)


class BaseSlice(CSTNode, ABC):
    """
    Any slice type that can slot into a :class:`SubscriptElement`.
    This node is purely for typing.
    """

    __slots__ = ()


@add_slots
@dataclass(frozen=True)
class Index(BaseSlice):
    """
    Any index as passed to a :class:`Subscript`. In ``x[2]``, this would be the ``2``
    value.
    """

    #: The index value itself.
    value: BaseExpression

    #: An optional string with an asterisk appearing before the name. This is
    #: expanded into variable number of positional arguments. See PEP-646
    star: Optional[Literal["*"]] = None

    #: Whitespace after the ``star`` (if it exists), but before the ``value``.
    whitespace_after_star: Optional[BaseParenthesizableWhitespace] = None

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Index":
        return Index(
            star=self.star,
            whitespace_after_star=visit_optional(
                self, "whitespace_after_star", self.whitespace_after_star, visitor
            ),
            value=visit_required(self, "value", self.value, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        star = self.star
        if star is not None:
            state.add_token(star)
        ws = self.whitespace_after_star
        if ws is not None:
            ws._codegen(state)
        self.value._codegen(state)


@add_slots
@dataclass(frozen=True)
class Slice(BaseSlice):
    """
    Any slice operation in a :class:`Subscript`, such as ``1:``, ``2:3:4``, etc.

    Note that the grammar does NOT allow parenthesis around a slice so they are not
    supported here.
    """

    #: The lower bound in the slice, if present
    lower: Optional[BaseExpression]

    #: The upper bound in the slice, if present
    upper: Optional[BaseExpression]

    #: The step in the slice, if present
    step: Optional[BaseExpression] = None

    #: The first slice operator
    first_colon: Colon = Colon.field()

    #: The second slice operator, usually omitted
    second_colon: Union[Colon, MaybeSentinel] = MaybeSentinel.DEFAULT

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Slice":
        return Slice(
            lower=visit_optional(self, "lower", self.lower, visitor),
            first_colon=visit_required(self, "first_colon", self.first_colon, visitor),
            upper=visit_optional(self, "upper", self.upper, visitor),
            second_colon=visit_sentinel(
                self, "second_colon", self.second_colon, visitor
            ),
            step=visit_optional(self, "step", self.step, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        lower = self.lower
        if lower is not None:
            lower._codegen(state)
        self.first_colon._codegen(state)
        upper = self.upper
        if upper is not None:
            upper._codegen(state)
        second_colon = self.second_colon
        if second_colon is MaybeSentinel.DEFAULT and self.step is not None:
            state.add_token(":")
        elif isinstance(second_colon, Colon):
            second_colon._codegen(state)
        step = self.step
        if step is not None:
            step._codegen(state)


@add_slots
@dataclass(frozen=True)
class SubscriptElement(CSTNode):
    """
    Part of a sequence of slices in a :class:`Subscript`, such as ``1:2, 3``. This is
    not used in Python's standard library, but it is used in some third-party
    libraries. For example, `NumPy uses it to select values and ranges from
    multi-dimensional arrays
    <https://docs.scipy.org/doc/numpy-1.10.1/user/basics.indexing.html>`_.
    """

    #: A slice or index that is part of a subscript.
    slice: BaseSlice

    #: A separating comma, with any whitespace it owns.
    comma: Union[Comma, MaybeSentinel] = MaybeSentinel.DEFAULT

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "SubscriptElement":
        return SubscriptElement(
            slice=visit_required(self, "slice", self.slice, visitor),
            comma=visit_sentinel(self, "comma", self.comma, visitor),
        )

    def _codegen_impl(self, state: CodegenState, default_comma: bool = False) -> None:
        with state.record_syntactic_position(self):
            self.slice._codegen(state)

        comma = self.comma
        if comma is MaybeSentinel.DEFAULT and default_comma:
            state.add_token(", ")
        elif isinstance(comma, Comma):
            comma._codegen(state)


@add_slots
@dataclass(frozen=True)
class Subscript(BaseAssignTargetExpression, BaseDelTargetExpression):
    """
    A indexed subscript reference (:class:`Index`) such as ``x[2]``, a :class:`Slice`
    such as ``x[1:-1]``, or an extended slice (:class:`SubscriptElement`) such as ``x[1:2, 3]``.
    """

    #: The left-hand expression which, when evaluated, will be subscripted, such as
    #: ``x`` in ``x[2]``.
    value: BaseExpression

    #: The :class:`SubscriptElement` to extract from the ``value``.
    slice: Sequence[SubscriptElement]

    lbracket: LeftSquareBracket = LeftSquareBracket.field()
    #: Brackets after the ``value`` surrounding the ``slice``.
    rbracket: RightSquareBracket = RightSquareBracket.field()

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    #: Whitespace after the ``value``, but before the ``lbracket``.
    whitespace_after_value: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    def _validate(self) -> None:
        super(Subscript, self)._validate()
        # Validate valid commas
        if len(self.slice) < 1:
            raise CSTValidationError("Cannot have empty SubscriptElement.")

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Subscript":
        return Subscript(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            value=visit_required(self, "value", self.value, visitor),
            whitespace_after_value=visit_required(
                self, "whitespace_after_value", self.whitespace_after_value, visitor
            ),
            lbracket=visit_required(self, "lbracket", self.lbracket, visitor),
            slice=visit_sequence(self, "slice", self.slice, visitor),
            rbracket=visit_required(self, "rbracket", self.rbracket, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _safe_to_use_with_word_operator(self, position: ExpressionPosition) -> bool:
        if position == ExpressionPosition.LEFT:
            return True
        if super(Subscript, self)._safe_to_use_with_word_operator(position):
            return True
        if position == ExpressionPosition.RIGHT:
            return self.value._safe_to_use_with_word_operator(ExpressionPosition.RIGHT)
        return False

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state):
            self.value._codegen(state)
            self.whitespace_after_value._codegen(state)
            self.lbracket._codegen(state)
            lastslice = len(self.slice) - 1
            for i, slice in enumerate(self.slice):
                slice._codegen(state, default_comma=(i != lastslice))
            self.rbracket._codegen(state)


@add_slots
@dataclass(frozen=True)
class Annotation(CSTNode):
    """
    An annotation for a function (`PEP 3107`_) or on a variable (`PEP 526`_). Typically
    these are used in the context of type hints (`PEP 484`_), such as::

        # a variable with a type
        good_ideas: List[str] = []

        # a function with type annotations
        def concat(substrings: Sequence[str]) -> str:
            ...

    .. _PEP 3107: https://www.python.org/dev/peps/pep-3107/
    .. _PEP 526: https://www.python.org/dev/peps/pep-0526/
    .. _PEP 484: https://www.python.org/dev/peps/pep-0484/
    """

    #: The annotation's value itself. This is the part of the annotation after the
    #: colon or arrow.
    annotation: BaseExpression

    whitespace_before_indicator: Union[BaseParenthesizableWhitespace, MaybeSentinel] = (
        MaybeSentinel.DEFAULT
    )
    whitespace_after_indicator: BaseParenthesizableWhitespace = SimpleWhitespace.field(
        " "
    )

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Annotation":
        return Annotation(
            whitespace_before_indicator=visit_sentinel(
                self,
                "whitespace_before_indicator",
                self.whitespace_before_indicator,
                visitor,
            ),
            whitespace_after_indicator=visit_required(
                self,
                "whitespace_after_indicator",
                self.whitespace_after_indicator,
                visitor,
            ),
            annotation=visit_required(self, "annotation", self.annotation, visitor),
        )

    def _codegen_impl(
        self, state: CodegenState, default_indicator: Optional[str] = None
    ) -> None:
        # First, figure out the indicator which tells us default whitespace.
        if default_indicator is None:
            raise CSTCodegenError(
                "Must specify a concrete default_indicator if default used on indicator."
            )

        # Now, output the whitespace
        whitespace_before_indicator = self.whitespace_before_indicator
        if isinstance(whitespace_before_indicator, BaseParenthesizableWhitespace):
            whitespace_before_indicator._codegen(state)
        elif isinstance(whitespace_before_indicator, MaybeSentinel):
            if default_indicator == "->":
                state.add_token(" ")
        else:
            raise CSTLogicError("Logic error!")

        # Now, output the indicator and the rest of the annotation
        state.add_token(default_indicator)
        self.whitespace_after_indicator._codegen(state)

        with state.record_syntactic_position(self):
            self.annotation._codegen(state)


@add_slots
@dataclass(frozen=True)
class ParamStar(CSTNode):
    """
    A sentinel indicator on a :class:`Parameters` list to denote that the subsequent
    params are keyword-only args.

    This syntax is described in `PEP 3102`_.

    .. _PEP 3102: https://www.python.org/dev/peps/pep-3102/#specification
    """

    # Comma that comes after the star.
    comma: Comma = Comma.field(whitespace_after=SimpleWhitespace(" "))

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "ParamStar":
        return ParamStar(comma=visit_required(self, "comma", self.comma, visitor))

    def _codegen_impl(self, state: CodegenState) -> None:
        state.add_token("*")
        self.comma._codegen(state)


@add_slots
@dataclass(frozen=True)
class ParamSlash(CSTNode):
    """
    A sentinel indicator on a :class:`Parameters` list to denote that the previous
    params are positional-only args.

    This syntax is described in `PEP 570`_.

    .. _PEP 570: https://www.python.org/dev/peps/pep-0570/#specification
    """

    #: Optional comma that comes after the slash. This comma doesn't own the whitespace
    #: between ``/`` and ``,``.
    comma: Union[Comma, MaybeSentinel] = MaybeSentinel.DEFAULT

    #: Whitespace after the ``/`` character. This is captured here in case there is a
    #: comma.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "ParamSlash":
        return ParamSlash(
            comma=visit_sentinel(self, "comma", self.comma, visitor),
            whitespace_after=visit_required(
                self, "whitespace_after", self.whitespace_after, visitor
            ),
        )

    def _codegen_impl(self, state: CodegenState, default_comma: bool = False) -> None:
        state.add_token("/")

        self.whitespace_after._codegen(state)
        comma = self.comma
        if comma is MaybeSentinel.DEFAULT and default_comma:
            state.add_token(", ")
        elif isinstance(comma, Comma):
            comma._codegen(state)


@add_slots
@dataclass(frozen=True)
class Param(CSTNode):
    """
    A positional or keyword argument in a :class:`Parameters` list. May contain an
    :class:`Annotation` and, in some cases, a ``default``.
    """

    #: The parameter name itself.
    name: Name

    #: Any optional :class:`Annotation`. These annotations are usually used as type
    #: hints.
    annotation: Optional[Annotation] = None

    #: The equal sign used to denote assignment if there is a default.
    equal: Union[AssignEqual, MaybeSentinel] = MaybeSentinel.DEFAULT

    #: Any optional default value, used when the argument is not supplied.
    default: Optional[BaseExpression] = None

    #: A trailing comma. If one is not provided, :class:`MaybeSentinel` will be
    #: replaced with a comma only if a comma is required.
    comma: Union[Comma, MaybeSentinel] = MaybeSentinel.DEFAULT

    #: Zero, one, or two asterisks appearing before name for :class:`Param`'s
    #: ``star_arg`` and ``star_kwarg``.
    star: Union[str, MaybeSentinel] = MaybeSentinel.DEFAULT

    #: The whitespace before ``name``. It will appear after ``star`` when a star
    #: exists.
    whitespace_after_star: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    #: The whitespace after this entire node.
    whitespace_after_param: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    def _validate(self) -> None:
        if self.default is None and isinstance(self.equal, AssignEqual):
            raise CSTValidationError(
                "Must have a default when specifying an AssignEqual."
            )
        if isinstance(self.star, str) and self.star not in ("", "*", "**"):
            raise CSTValidationError("Must specify either '', '*' or '**' for star.")

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Param":
        return Param(
            star=self.star,
            whitespace_after_star=visit_required(
                self, "whitespace_after_star", self.whitespace_after_star, visitor
            ),
            name=visit_required(self, "name", self.name, visitor),
            annotation=visit_optional(self, "annotation", self.annotation, visitor),
            equal=visit_sentinel(self, "equal", self.equal, visitor),
            default=visit_optional(self, "default", self.default, visitor),
            comma=visit_sentinel(self, "comma", self.comma, visitor),
            whitespace_after_param=visit_required(
                self, "whitespace_after_param", self.whitespace_after_param, visitor
            ),
        )

    def _codegen_impl(
        self,
        state: CodegenState,
        default_star: Optional[str] = None,
        default_comma: bool = False,
    ) -> None:
        with state.record_syntactic_position(self):
            star = self.star
            if isinstance(star, MaybeSentinel):
                if default_star is None:
                    raise CSTCodegenError(
                        "Must specify a concrete default_star if default used on star."
                    )
                star = default_star
            if isinstance(star, str):
                state.add_token(star)
            self.whitespace_after_star._codegen(state)
            self.name._codegen(state)

        annotation = self.annotation
        if annotation is not None:
            annotation._codegen(state, default_indicator=":")
        equal = self.equal
        if equal is MaybeSentinel.DEFAULT and self.default is not None:
            state.add_token(" = ")
        elif isinstance(equal, AssignEqual):
            equal._codegen(state)
        default = self.default
        if default is not None:
            default._codegen(state)
        comma = self.comma
        if comma is MaybeSentinel.DEFAULT and default_comma:
            state.add_token(", ")
        elif isinstance(comma, Comma):
            comma._codegen(state)

        self.whitespace_after_param._codegen(state)


@add_slots
@dataclass(frozen=True)
class Parameters(CSTNode):
    """
    A function or lambda parameter list.
    """

    #: Positional parameters, with or without defaults. Positional parameters
    #: with defaults must all be after those without defaults.
    params: Sequence[Param] = ()

    # Optional parameter that captures unspecified positional arguments or a sentinel
    # star that dictates parameters following are kwonly args.
    star_arg: Union[Param, ParamStar, MaybeSentinel] = MaybeSentinel.DEFAULT

    #: Keyword-only params that may or may not have defaults.
    kwonly_params: Sequence[Param] = ()

    #: Optional parameter that captures unspecified kwargs.
    star_kwarg: Optional[Param] = None

    #: Positional-only parameters, with or without defaults. Positional-only
    #: parameters with defaults must all be after those without defaults.
    posonly_params: Sequence[Param] = ()

    #: Optional sentinel that dictates parameters preceeding are positional-only
    #: args.
    posonly_ind: Union[ParamSlash, MaybeSentinel] = MaybeSentinel.DEFAULT

    def _validate_stars_sequence(self, vals: Sequence[Param], *, section: str) -> None:
        if len(vals) == 0:
            return
        for val in vals:
            if isinstance(val.star, str) and val.star != "":
                raise CSTValidationError(
                    f"Expecting a star prefix of '' for {section} Param."
                )

    def _validate_posonly_ind(self) -> None:
        if isinstance(self.posonly_ind, ParamSlash) and len(self.posonly_params) == 0:
            raise CSTValidationError(
                "Must have at least one posonly param if ParamSlash is used."
            )

    def _validate_kwonly_star(self) -> None:
        if isinstance(self.star_arg, ParamStar) and len(self.kwonly_params) == 0:
            raise CSTValidationError(
                "Must have at least one kwonly param if ParamStar is used."
            )

    def _validate_defaults(self) -> None:
        seen_default = False
        # pyre-fixme[60]: Concatenation not yet support for multiple variadic
        #  tuples: `*self.posonly_params, *self.params`.
        for param in (*self.posonly_params, *self.params):
            if param.default:
                # Mark that we've moved onto defaults
                if not seen_default:
                    seen_default = True
            else:
                if seen_default:
                    # We accidentally included a non-default after a default arg!
                    raise CSTValidationError(
                        "Cannot have param without defaults following a param with defaults."
                    )
        star_arg = self.star_arg
        if isinstance(star_arg, Param) and star_arg.default is not None:
            raise CSTValidationError("Cannot have default for star_arg.")
        star_kwarg = self.star_kwarg
        if star_kwarg is not None and star_kwarg.default is not None:
            raise CSTValidationError("Cannot have default for star_kwarg.")

    def _validate_stars(self) -> None:
        if len(self.params) > 0:
            self._validate_stars_sequence(self.params, section="params")
        if len(self.posonly_params) > 0:
            self._validate_stars_sequence(self.posonly_params, section="posonly_params")
        star_arg = self.star_arg
        if (
            isinstance(star_arg, Param)
            and isinstance(star_arg.star, str)
            and star_arg.star != "*"
        ):
            raise CSTValidationError(
                "Expecting a star prefix of '*' for star_arg Param."
            )
        if len(self.kwonly_params) > 0:
            self._validate_stars_sequence(self.kwonly_params, section="kwonly_params")
        star_kwarg = self.star_kwarg
        if (
            star_kwarg is not None
            and isinstance(star_kwarg.star, str)
            and star_kwarg.star != "**"
        ):
            raise CSTValidationError(
                "Expecting a star prefix of '**' for star_kwarg Param."
            )

    def _validate(self) -> None:
        # Validate posonly_params slash placement semantics.
        self._validate_posonly_ind()
        # Validate kwonly_param star placement semantics.
        self._validate_kwonly_star()
        # Validate defaults semantics for params and star_arg/star_kwarg.
        self._validate_defaults()
        # Validate that we don't have random stars on non star_kwarg.
        self._validate_stars()

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Parameters":
        return Parameters(
            posonly_params=visit_sequence(
                self, "posonly_params", self.posonly_params, visitor
            ),
            posonly_ind=visit_sentinel(self, "posonly_ind", self.posonly_ind, visitor),
            params=visit_sequence(self, "params", self.params, visitor),
            star_arg=visit_sentinel(self, "star_arg", self.star_arg, visitor),
            kwonly_params=visit_sequence(
                self, "kwonly_params", self.kwonly_params, visitor
            ),
            star_kwarg=visit_optional(self, "star_kwarg", self.star_kwarg, visitor),
        )

    def _safe_to_join_with_lambda(self) -> bool:
        """
        Determine if Parameters need a space after the `lambda` keyword. Returns True
        iff it's safe to omit the space between `lambda` and these Parameters.

        See also `BaseExpression._safe_to_use_with_word_operator`.

        For example: `lambda*_: pass`
        """
        if len(self.posonly_params) != 0:
            return False

        # posonly_ind can't appear if above condition is false

        if len(self.params) > 0 and self.params[0].star not in {"*", "**"}:
            return False

        return True

    def _codegen_impl(self, state: CodegenState) -> None:  # noqa: C901
        # Compute the star existence first so we can ask about whether
        # each element is the last in the list or not.
        star_arg = self.star_arg
        if isinstance(star_arg, MaybeSentinel):
            starincluded = len(self.kwonly_params) > 0
        elif isinstance(star_arg, (Param, ParamStar)):
            starincluded = True
        else:
            starincluded = False
        # Render out the positional-only params first. They will always have trailing
        # commas because in order to have positional-only params, there must be a
        # slash afterwards.
        for i, param in enumerate(self.posonly_params):
            param._codegen(state, default_star="", default_comma=True)
        # Render out the positional-only indicator if necessary.
        more_values = (
            starincluded
            or len(self.params) > 0
            or len(self.kwonly_params) > 0
            or self.star_kwarg is not None
        )
        posonly_ind = self.posonly_ind
        if isinstance(posonly_ind, ParamSlash):
            # Its explicitly included, so render the version we have here which
            # might have spacing applied to its comma.
            posonly_ind._codegen(state, default_comma=more_values)
        elif len(self.posonly_params) > 0:
            if more_values:
                state.add_token("/, ")
            else:
                state.add_token("/")
        # Render out the params next, computing necessary trailing commas.
        lastparam = len(self.params) - 1
        more_values = (
            starincluded or len(self.kwonly_params) > 0 or self.star_kwarg is not None
        )
        for i, param in enumerate(self.params):
            param._codegen(
                state, default_star="", default_comma=(i < lastparam or more_values)
            )
        # Render out optional star sentinel if its explicitly included or
        # if we are inferring it from kwonly_params. Otherwise, render out the
        # optional star_arg.
        if isinstance(star_arg, MaybeSentinel):
            if starincluded:
                state.add_token("*, ")
        elif isinstance(star_arg, Param):
            more_values = len(self.kwonly_params) > 0 or self.star_kwarg is not None
            star_arg._codegen(state, default_star="*", default_comma=more_values)
        elif isinstance(star_arg, ParamStar):
            star_arg._codegen(state)
        # Render out the kwonly_args next, computing necessary trailing commas.
        lastparam = len(self.kwonly_params) - 1
        more_values = self.star_kwarg is not None
        for i, param in enumerate(self.kwonly_params):
            param._codegen(
                state, default_star="", default_comma=(i < lastparam or more_values)
            )
        # Finally, render out any optional star_kwarg
        star_kwarg = self.star_kwarg
        if star_kwarg is not None:
            star_kwarg._codegen(state, default_star="**", default_comma=False)


@add_slots
@dataclass(frozen=True)
class Lambda(BaseExpression):
    """
    A lambda expression that creates an anonymous function.

    ::

        Lambda(
            params=Parameters([Param(Name("arg"))]),
            body=Ellipsis(),
        )

    Represents the following code::

        lambda arg: ...

    Named functions statements are provided by :class:`FunctionDef`.
    """

    #: The arguments to the lambda. This is similar to the arguments on a
    #: :class:`FunctionDef`, however lambda arguments are not allowed to have an
    #: :class:`Annotation`.
    params: Parameters

    #: The value that the lambda computes and returns when called.
    body: BaseExpression

    #: The colon separating the parameters from the body.
    colon: Colon = Colon.field(whitespace_after=SimpleWhitespace(" "))

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    #: Whitespace after the lambda keyword, but before any argument or the colon.
    whitespace_after_lambda: Union[BaseParenthesizableWhitespace, MaybeSentinel] = (
        MaybeSentinel.DEFAULT
    )

    def _safe_to_use_with_word_operator(self, position: ExpressionPosition) -> bool:
        if position == ExpressionPosition.LEFT:
            return len(self.rpar) > 0 or self.body._safe_to_use_with_word_operator(
                position
            )
        return super()._safe_to_use_with_word_operator(position)

    def _validate(self) -> None:
        # Validate parents
        super(Lambda, self)._validate()
        # Sum up all parameters
        all_params = [
            *self.params.posonly_params,
            *self.params.params,
            *self.params.kwonly_params,
        ]
        star_arg = self.params.star_arg
        if isinstance(star_arg, Param):
            all_params.append(star_arg)
        star_kwarg = self.params.star_kwarg
        if star_kwarg is not None:
            all_params.append(star_kwarg)
        # Check for nonzero parameters because several checks care
        # about this.
        if len(all_params) > 0:
            for param in all_params:
                if param.annotation is not None:
                    raise CSTValidationError(
                        "Lambda params cannot have type annotations."
                    )
            whitespace_after_lambda = self.whitespace_after_lambda
            if (
                isinstance(whitespace_after_lambda, BaseParenthesizableWhitespace)
                and whitespace_after_lambda.empty
                and not self.params._safe_to_join_with_lambda()
            ):
                raise CSTValidationError(
                    "Must have at least one space after lambda when specifying params"
                )

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Lambda":
        return Lambda(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            whitespace_after_lambda=visit_sentinel(
                self, "whitespace_after_lambda", self.whitespace_after_lambda, visitor
            ),
            params=visit_required(self, "params", self.params, visitor),
            colon=visit_required(self, "colon", self.colon, visitor),
            body=visit_required(self, "body", self.body, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state):
            state.add_token("lambda")
            whitespace_after_lambda = self.whitespace_after_lambda
            if isinstance(whitespace_after_lambda, MaybeSentinel):
                if not (
                    len(self.params.posonly_params) == 0
                    and len(self.params.params) == 0
                    and not isinstance(self.params.star_arg, Param)
                    and len(self.params.kwonly_params) == 0
                    and self.params.star_kwarg is None
                ):
                    # We have one or more params, provide a space
                    state.add_token(" ")
            elif isinstance(whitespace_after_lambda, BaseParenthesizableWhitespace):
                whitespace_after_lambda._codegen(state)
            self.params._codegen(state)
            self.colon._codegen(state)
            self.body._codegen(state)


@add_slots
@dataclass(frozen=True)
class Arg(CSTNode):
    """
    A single argument to a :class:`Call`.

    This supports named keyword arguments in the form of ``keyword=value`` and variable
    argument expansion using ``*args`` or ``**kwargs`` syntax.
    """

    #: The argument expression itself, not including a preceding keyword, or any of
    #: the surrounding the value, like a comma or asterisks.
    value: BaseExpression

    #: Optional keyword for the argument.
    keyword: Optional[Name] = None

    #: The equal sign used to denote assignment if there is a keyword.
    equal: Union[AssignEqual, MaybeSentinel] = MaybeSentinel.DEFAULT

    #: Any trailing comma.
    comma: Union[Comma, MaybeSentinel] = MaybeSentinel.DEFAULT

    #: A string with zero, one, or two asterisks appearing before the name. These are
    #: expanded into variable number of positional or keyword arguments.
    star: Literal["", "*", "**"] = ""

    #: Whitespace after the ``star`` (if it exists), but before the ``keyword`` or
    #: ``value`` (if no keyword is provided).
    whitespace_after_star: BaseParenthesizableWhitespace = SimpleWhitespace.field("")
    #: Whitespace after this entire node. The :class:`Comma` node (if it exists) may
    #: also store some trailing whitespace.
    whitespace_after_arg: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    def _validate(self) -> None:
        if self.keyword is None and isinstance(self.equal, AssignEqual):
            raise CSTValidationError(
                "Must have a keyword when specifying an AssignEqual."
            )
        if self.star not in ("", "*", "**"):
            raise CSTValidationError("Must specify either '', '*' or '**' for star.")
        if self.star in ("*", "**") and self.keyword is not None:
            raise CSTValidationError("Cannot specify a star and a keyword together.")

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Arg":
        return Arg(
            star=self.star,
            whitespace_after_star=visit_required(
                self, "whitespace_after_star", self.whitespace_after_star, visitor
            ),
            keyword=visit_optional(self, "keyword", self.keyword, visitor),
            equal=visit_sentinel(self, "equal", self.equal, visitor),
            value=visit_required(self, "value", self.value, visitor),
            comma=visit_sentinel(self, "comma", self.comma, visitor),
            whitespace_after_arg=visit_required(
                self, "whitespace_after_arg", self.whitespace_after_arg, visitor
            ),
        )

    def _codegen_impl(self, state: CodegenState, default_comma: bool = False) -> None:
        with state.record_syntactic_position(self):
            state.add_token(self.star)
            self.whitespace_after_star._codegen(state)
            keyword = self.keyword
            if keyword is not None:
                keyword._codegen(state)
            equal = self.equal
            if equal is MaybeSentinel.DEFAULT and self.keyword is not None:
                state.add_token(" = ")
            elif isinstance(equal, AssignEqual):
                equal._codegen(state)
            self.value._codegen(state)

        comma = self.comma
        if comma is MaybeSentinel.DEFAULT and default_comma:
            state.add_token(", ")
        elif isinstance(comma, Comma):
            comma._codegen(state)
        self.whitespace_after_arg._codegen(state)


class _BaseExpressionWithArgs(BaseExpression, ABC):
    """
    Arguments are complicated enough that we can't represent them easily
    in typing. So, we have common validation functions here.
    """

    __slots__ = ()

    #: Sequence of arguments that will be passed to the function call.
    args: Sequence[Arg] = ()

    def _check_kwargs_or_keywords(self, arg: Arg) -> None:
        """
        Validates that we only have a mix of "keyword=arg" and "**arg" expansion.
        """

        if arg.keyword is not None:
            # Valid, keyword argument
            return None
        elif arg.star == "**":
            # Valid, kwargs
            return None
        elif arg.star == "*":
            # Invalid, cannot have "*" follow "**"
            raise CSTValidationError(
                "Cannot have iterable argument unpacking after keyword argument unpacking."
            )
        else:
            # Invalid, cannot have positional argument follow **/keyword
            raise CSTValidationError(
                "Cannot have positional argument after keyword argument unpacking."
            )

    def _check_starred_or_keywords(
        self, arg: Arg
    ) -> Optional[Callable[[Arg], Callable[[Arg], None]]]:
        """
        Validates that we only have a mix of "*arg" expansion and "keyword=arg".
        """

        if arg.keyword is not None:
            # Valid, keyword argument
            return None
        elif arg.star == "**":
            # Valid, but we now no longer allow "*" args
            # pyre-fixme[7]: Expected `Optional[Callable[[Arg], Callable[...,
            #  Any]]]` but got `Callable[[Arg], Optional[Callable[[Arg], Callable[...,
            #  Any]]]]`.
            return self._check_kwargs_or_keywords
        elif arg.star == "*":
            # Valid, iterable unpacking
            return None
        else:
            # Invalid, cannot have positional argument follow **/keyword
            raise CSTValidationError(
                "Cannot have positional argument after keyword argument."
            )

    def _check_positional(
        self, arg: Arg
    ) -> Optional[Callable[[Arg], Callable[[Arg], Callable[[Arg], None]]]]:
        """
        Validates that we only have a mix of positional args and "*arg" expansion.
        """

        if arg.keyword is not None:
            # Valid, but this puts us into starred/keyword state
            # pyre-fixme[7]: Expected `Optional[Callable[[Arg], Callable[...,
            #  Any]]]` but got `Callable[[Arg], Optional[Callable[[Arg], Callable[...,
            #  Any]]]]`.
            return self._check_starred_or_keywords
        elif arg.star == "**":
            # Valid, but we skip states to kwargs/keywords
            # pyre-fixme[7]: Expected `Optional[Callable[[Arg], Callable[...,
            #  Any]]]` but got `Callable[[Arg], Optional[Callable[[Arg], Callable[...,
            #  Any]]]]`.
            return self._check_kwargs_or_keywords
        elif arg.star == "*":
            # Valid, iterator expansion
            return None
        else:
            # Valid, allowed to have positional arguments here
            return None

    # pyre-fixme[30]: Pyre gave up inferring some types - function `_validate` was
    #  too complex.
    def _validate(self) -> None:
        # Validate any super-class stuff, whatever it may be.
        super()._validate()
        # Now, validate the weird intermingling rules for arguments by running
        # a small validator state machine. This works by passing each argument
        # to a validator function which can either raise an exception if it
        # detects an invalid sequence, return a new validator to be used for the
        # next arg, or return None to use the same validator. We could enforce
        # always returning ourselves instead of None but it ends up making the
        # functions themselves less readable. In this way, the current validator
        # function encodes the state we're in (positional state, iterable
        # expansion state, or dictionary expansion state).
        validator = self._check_positional
        for arg in self.args:
            validator = validator(arg) or validator


@add_slots
@dataclass(frozen=True)
class Call(_BaseExpressionWithArgs):
    """
    An expression representing a function call, such as ``do_math(1, 2)`` or
    ``picture.post_on_instagram()``.

    Function calls consist of a function name and a sequence of arguments wrapped in
    :class:`Arg` nodes.
    """

    #: The expression resulting in a callable that we are to call. Often a :class:`Name`
    #: or :class:`Attribute`.
    func: BaseExpression

    #: The arguments to pass to the resulting callable. These may be a mix of
    #: positional arguments, keyword arguments, or "starred" arguments.
    args: Sequence[Arg] = ()

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation. These are not the parenthesis
    #: before and after the list of ``args``, but rather arguments around the entire
    #: call expression, such as ``(( do_math(1, 2) ))``.
    rpar: Sequence[RightParen] = ()

    #: Whitespace after the ``func`` name, but before the opening parenthesis.
    whitespace_after_func: BaseParenthesizableWhitespace = SimpleWhitespace.field("")
    #: Whitespace after the opening parenthesis but before the first argument (if there
    #: are any). Whitespace after the last argument but before the closing parenthesis
    #: is owned by the last :class:`Arg` if it exists.
    whitespace_before_args: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    def _safe_to_use_with_word_operator(self, position: ExpressionPosition) -> bool:
        """
        Calls have a close paren on the right side regardless of whether they're
        parenthesized as a whole. As a result, they are safe to use directly against
        an adjacent node to the right.
        """
        if position == ExpressionPosition.LEFT:
            return True
        if super(Call, self)._safe_to_use_with_word_operator(position):
            return True
        if position == ExpressionPosition.RIGHT:
            return self.func._safe_to_use_with_word_operator(ExpressionPosition.RIGHT)
        return False

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Call":
        return Call(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            func=visit_required(self, "func", self.func, visitor),
            whitespace_after_func=visit_required(
                self, "whitespace_after_func", self.whitespace_after_func, visitor
            ),
            whitespace_before_args=visit_required(
                self, "whitespace_before_args", self.whitespace_before_args, visitor
            ),
            args=visit_sequence(self, "args", self.args, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state):
            self.func._codegen(state)
            self.whitespace_after_func._codegen(state)
            state.add_token("(")
            self.whitespace_before_args._codegen(state)
            lastarg = len(self.args) - 1
            for i, arg in enumerate(self.args):
                arg._codegen(state, default_comma=(i != lastarg))
            state.add_token(")")


@add_slots
@dataclass(frozen=True)
class Await(BaseExpression):
    """
    An await expression. Await expressions are only valid inside the body of an
    asynchronous :class:`FunctionDef` or (as of Python 3.7) inside of an asynchronous
    :class:`GeneratorExp` nodes.
    """

    #: The actual expression we need to wait for.
    expression: BaseExpression

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    #: Whitespace that appears after the ``async`` keyword, but before the inner
    #: ``expression``.
    whitespace_after_await: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _validate(self) -> None:
        # Validate any super-class stuff, whatever it may be.
        super(Await, self)._validate()
        # Make sure we don't run identifiers together.
        if (
            self.whitespace_after_await.empty
            and not self.expression._safe_to_use_with_word_operator(
                ExpressionPosition.RIGHT
            )
        ):
            raise CSTValidationError("Must have at least one space after await")

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Await":
        return Await(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            whitespace_after_await=visit_required(
                self, "whitespace_after_await", self.whitespace_after_await, visitor
            ),
            expression=visit_required(self, "expression", self.expression, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state):
            state.add_token("await")
            self.whitespace_after_await._codegen(state)
            self.expression._codegen(state)


@add_slots
@dataclass(frozen=True)
class IfExp(BaseExpression):
    """
    An if expression of the form ``body if test else orelse``.

    If statements are provided by :class:`If` and :class:`Else` nodes.
    """

    #: The test to perform.
    test: BaseExpression

    #: The expression to evaluate when the test is true.
    body: BaseExpression

    #: The expression to evaluate when the test is false.
    orelse: BaseExpression

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    #: Whitespace after the ``body`` expression, but before the ``if`` keyword.
    whitespace_before_if: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Whitespace after the ``if`` keyword, but before the ``test`` clause.
    whitespace_after_if: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Whitespace after the ``test`` expression, but before the ``else`` keyword.
    whitespace_before_else: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Whitespace after the ``else`` keyword, but before the ``orelse`` expression.
    whitespace_after_else: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _safe_to_use_with_word_operator(self, position: ExpressionPosition) -> bool:
        if position == ExpressionPosition.RIGHT:
            return self.body._safe_to_use_with_word_operator(position)
        else:
            return self.orelse._safe_to_use_with_word_operator(position)

    def _validate(self) -> None:
        # Paren validation and such
        super(IfExp, self)._validate()
        # Validate spacing rules
        if (
            self.whitespace_before_if.empty
            and not self.body._safe_to_use_with_word_operator(ExpressionPosition.LEFT)
        ):
            raise CSTValidationError(
                "Must have at least one space before 'if' keyword."
            )
        if (
            self.whitespace_after_if.empty
            and not self.test._safe_to_use_with_word_operator(ExpressionPosition.RIGHT)
        ):
            raise CSTValidationError("Must have at least one space after 'if' keyword.")
        if (
            self.whitespace_before_else.empty
            and not self.test._safe_to_use_with_word_operator(ExpressionPosition.LEFT)
        ):
            raise CSTValidationError(
                "Must have at least one space before 'else' keyword."
            )
        if (
            self.whitespace_after_else.empty
            and not self.orelse._safe_to_use_with_word_operator(
                ExpressionPosition.RIGHT
            )
        ):
            raise CSTValidationError(
                "Must have at least one space after 'else' keyword."
            )

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "IfExp":
        return IfExp(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            body=visit_required(self, "body", self.body, visitor),
            whitespace_before_if=visit_required(
                self, "whitespace_before_if", self.whitespace_before_if, visitor
            ),
            whitespace_after_if=visit_required(
                self, "whitespace_after_if", self.whitespace_after_if, visitor
            ),
            test=visit_required(self, "test", self.test, visitor),
            whitespace_before_else=visit_required(
                self, "whitespace_before_else", self.whitespace_before_else, visitor
            ),
            whitespace_after_else=visit_required(
                self, "whitespace_after_else", self.whitespace_after_else, visitor
            ),
            orelse=visit_required(self, "orelse", self.orelse, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state):
            self.body._codegen(state)
            self.whitespace_before_if._codegen(state)
            state.add_token("if")
            self.whitespace_after_if._codegen(state)
            self.test._codegen(state)
            self.whitespace_before_else._codegen(state)
            state.add_token("else")
            self.whitespace_after_else._codegen(state)
            self.orelse._codegen(state)


@add_slots
@dataclass(frozen=True)
class From(CSTNode):
    """
    A ``from x`` stanza in a :class:`Yield` or :class:`Raise`.
    """

    #: The expression that we are yielding/raising from.
    item: BaseExpression

    #: The whitespace at the very start of this node.
    whitespace_before_from: Union[BaseParenthesizableWhitespace, MaybeSentinel] = (
        MaybeSentinel.DEFAULT
    )

    #: The whitespace after the ``from`` keyword, but before the ``item``.
    whitespace_after_from: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _validate(self) -> None:
        if (
            isinstance(self.whitespace_after_from, BaseParenthesizableWhitespace)
            and self.whitespace_after_from.empty
            and not self.item._safe_to_use_with_word_operator(ExpressionPosition.RIGHT)
        ):
            raise CSTValidationError(
                "Must have at least one space after 'from' keyword."
            )

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "From":
        return From(
            whitespace_before_from=visit_sentinel(
                self, "whitespace_before_from", self.whitespace_before_from, visitor
            ),
            whitespace_after_from=visit_required(
                self, "whitespace_after_from", self.whitespace_after_from, visitor
            ),
            item=visit_required(self, "item", self.item, visitor),
        )

    def _codegen_impl(self, state: CodegenState, default_space: str = "") -> None:
        whitespace_before_from = self.whitespace_before_from
        if isinstance(whitespace_before_from, BaseParenthesizableWhitespace):
            whitespace_before_from._codegen(state)
        else:
            state.add_token(default_space)

        with state.record_syntactic_position(self):
            state.add_token("from")
            self.whitespace_after_from._codegen(state)
            self.item._codegen(state)


@add_slots
@dataclass(frozen=True)
class Yield(BaseExpression):
    """
    A yield expression similar to ``yield x`` or ``yield from fun()``.

    To learn more about the ways that yield can be used in generators, refer to
    `Python's language reference
    <https://docs.python.org/3/reference/expressions.html#yieldexpr>`__.
    """

    #: The value yielded from the generator, in the case of a :class:`From` clause, a
    #: sub-generator to iterate over.
    value: Optional[Union[BaseExpression, From]] = None

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    #: Whitespace after the ``yield`` keyword, but before the ``value``.
    whitespace_after_yield: Union[BaseParenthesizableWhitespace, MaybeSentinel] = (
        MaybeSentinel.DEFAULT
    )

    def _validate(self) -> None:
        # Paren rules and such
        super(Yield, self)._validate()
        # Our own rules
        whitespace_after_yield = self.whitespace_after_yield
        if (
            isinstance(whitespace_after_yield, BaseParenthesizableWhitespace)
            and whitespace_after_yield.empty
        ):
            value = self.value
            if isinstance(value, From):
                raise CSTValidationError(
                    "Must have at least one space after 'yield' keyword."
                )
            if isinstance(
                value, BaseExpression
            ) and not value._safe_to_use_with_word_operator(ExpressionPosition.RIGHT):
                raise CSTValidationError(
                    "Must have at least one space after 'yield' keyword."
                )

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Yield":
        return Yield(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            whitespace_after_yield=visit_sentinel(
                self, "whitespace_after_yield", self.whitespace_after_yield, visitor
            ),
            value=visit_optional(self, "value", self.value, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state):
            state.add_token("yield")
            whitespace_after_yield = self.whitespace_after_yield
            if isinstance(whitespace_after_yield, BaseParenthesizableWhitespace):
                whitespace_after_yield._codegen(state)
            else:
                # Only need a space after yield if there is a value to yield.
                if self.value is not None:
                    state.add_token(" ")
            value = self.value
            if isinstance(value, From):
                value._codegen(state, default_space="")
            elif value is not None:
                value._codegen(state)


class _BaseElementImpl(CSTNode, ABC):
    """
    An internal base class for :class:`Element` and :class:`DictElement`.
    """

    __slots__ = ()

    value: BaseExpression
    comma: Union[Comma, MaybeSentinel] = MaybeSentinel.DEFAULT

    def _codegen_comma(
        self,
        state: CodegenState,
        default_comma: bool = False,
        default_comma_whitespace: bool = False,  # False for a single-item collection
    ) -> None:
        """
        Called by `_codegen_impl` in subclasses to generate the comma.
        """
        comma = self.comma
        if comma is MaybeSentinel.DEFAULT and default_comma:
            if default_comma_whitespace:
                state.add_token(", ")
            else:
                state.add_token(",")
        elif isinstance(comma, Comma):
            comma._codegen(state)

    @abstractmethod
    def _codegen_impl(
        self,
        state: CodegenState,
        default_comma: bool = False,
        default_comma_whitespace: bool = False,  # False for a single-item collection
    ) -> None: ...


class BaseElement(_BaseElementImpl, ABC):
    """
    An element of a literal list, tuple, or set. For elements of a literal dict, see
    BaseDictElement.
    """

    __slots__ = ()


class BaseDictElement(_BaseElementImpl, ABC):
    """
    An element of a literal dict. For elements of a list, tuple, or set, see
    BaseElement.
    """

    __slots__ = ()


@add_slots
@dataclass(frozen=True)
class Element(BaseElement):
    """
    A simple value in a literal :class:`List`, :class:`Tuple`, or :class:`Set`.
    These a literal collection may also contain a :class:`StarredElement`.

    If you're using a literal :class:`Dict`, see :class:`DictElement` instead.
    """

    value: BaseExpression

    #: A trailing comma. By default, we'll only insert a comma if one is required.
    comma: Union[Comma, MaybeSentinel] = MaybeSentinel.DEFAULT

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Element":
        return Element(
            value=visit_required(self, "value", self.value, visitor),
            comma=visit_sentinel(self, "comma", self.comma, visitor),
        )

    def _codegen_impl(
        self,
        state: CodegenState,
        default_comma: bool = False,
        default_comma_whitespace: bool = False,
    ) -> None:
        with state.record_syntactic_position(self):
            self.value._codegen(state)
        self._codegen_comma(state, default_comma, default_comma_whitespace)


@add_slots
@dataclass(frozen=True)
class DictElement(BaseDictElement):
    """
    A simple ``key: value`` pair that represents a single entry in a literal
    :class:`Dict`. :class:`Dict` nodes may also contain a
    :class:`StarredDictElement`.

    If you're using a literal :class:`List`, :class:`Tuple`, or :class:`Set`,
    see :class:`Element` instead.
    """

    key: BaseExpression
    value: BaseExpression

    #: A trailing comma. By default, we'll only insert a comma if one is required.
    comma: Union[Comma, MaybeSentinel] = MaybeSentinel.DEFAULT

    #: Whitespace after the key, but before the colon in ``key : value``.
    whitespace_before_colon: BaseParenthesizableWhitespace = SimpleWhitespace.field("")
    #: Whitespace after the colon, but before the value in ``key : value``.
    whitespace_after_colon: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "DictElement":
        return DictElement(
            key=visit_required(self, "key", self.key, visitor),
            whitespace_before_colon=visit_required(
                self, "whitespace_before_colon", self.whitespace_before_colon, visitor
            ),
            whitespace_after_colon=visit_required(
                self, "whitespace_after_colon", self.whitespace_after_colon, visitor
            ),
            value=visit_required(self, "value", self.value, visitor),
            comma=visit_sentinel(self, "comma", self.comma, visitor),
        )

    def _codegen_impl(
        self,
        state: CodegenState,
        default_comma: bool = False,
        default_comma_whitespace: bool = False,
    ) -> None:
        with state.record_syntactic_position(self):
            self.key._codegen(state)
            self.whitespace_before_colon._codegen(state)
            state.add_token(":")
            self.whitespace_after_colon._codegen(state)
            self.value._codegen(state)
        self._codegen_comma(state, default_comma, default_comma_whitespace)


@add_slots
@dataclass(frozen=True)
class StarredElement(BaseElement, BaseExpression, _BaseParenthesizedNode):
    """
    A starred ``*value`` element that expands to represent multiple values in a literal
    :class:`List`, :class:`Tuple`, or :class:`Set`.

    If you're using a literal :class:`Dict`, see :class:`StarredDictElement` instead.

    If this node owns parenthesis, those parenthesis wrap the leading asterisk, but not
    the trailing comma. For example::

        StarredElement(
            cst.Name("el"),
            comma=cst.Comma(),
            lpar=[cst.LeftParen()],
            rpar=[cst.RightParen()],
        )

    will generate::

        (*el),
    """

    value: BaseExpression

    #: A trailing comma. By default, we'll only insert a comma if one is required.
    comma: Union[Comma, MaybeSentinel] = MaybeSentinel.DEFAULT

    #: Parenthesis at the beginning of the node, before the leading asterisk.
    lpar: Sequence[LeftParen] = ()
    #: Parentheses after the value, but before a comma (if there is one).
    rpar: Sequence[RightParen] = ()

    #: Whitespace between the leading asterisk and the value expression.
    whitespace_before_value: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "StarredElement":
        return StarredElement(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            whitespace_before_value=visit_required(
                self, "whitespace_before_value", self.whitespace_before_value, visitor
            ),
            value=visit_required(self, "value", self.value, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
            comma=visit_sentinel(self, "comma", self.comma, visitor),
        )

    def _codegen_impl(
        self,
        state: CodegenState,
        default_comma: bool = False,
        default_comma_whitespace: bool = False,
    ) -> None:
        with self._parenthesize(state):
            state.add_token("*")
            self.whitespace_before_value._codegen(state)
            self.value._codegen(state)
        self._codegen_comma(state, default_comma, default_comma_whitespace)


@add_slots
@dataclass(frozen=True)
class StarredDictElement(BaseDictElement):
    """
    A starred ``**value`` element that expands to represent multiple values in a literal
    :class:`Dict`.

    If you're using a literal :class:`List`, :class:`Tuple`, or :class:`Set`,
    see :class:`StarredElement` instead.

    Unlike :class:`StarredElement`, this node does not own left or right parenthesis,
    but the ``value`` field may still contain parenthesis. This is due to some
    asymmetry in Python's grammar.
    """

    value: BaseExpression

    #: A trailing comma. By default, we'll only insert a comma if one is required.
    comma: Union[Comma, MaybeSentinel] = MaybeSentinel.DEFAULT

    #: Whitespace between the leading asterisks and the value expression.
    whitespace_before_value: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "StarredDictElement":
        return StarredDictElement(
            whitespace_before_value=visit_required(
                self, "whitespace_before_value", self.whitespace_before_value, visitor
            ),
            value=visit_required(self, "value", self.value, visitor),
            comma=visit_sentinel(self, "comma", self.comma, visitor),
        )

    def _codegen_impl(
        self,
        state: CodegenState,
        default_comma: bool = False,
        default_comma_whitespace: bool = False,
    ) -> None:
        with state.record_syntactic_position(self):
            state.add_token("**")
            self.whitespace_before_value._codegen(state)
            self.value._codegen(state)
        self._codegen_comma(state, default_comma, default_comma_whitespace)


@add_slots
@dataclass(frozen=True)
class Tuple(BaseAssignTargetExpression, BaseDelTargetExpression):
    """
    An immutable literal tuple. Tuples are often (but not always) parenthesized.

    ::

        Tuple([
            Element(Integer("1")),
            Element(Integer("2")),
            StarredElement(Name("others")),
        ])

    generates the following code::

        (1, 2, *others)
    """

    #: A sequence containing all the :class:`Element` and :class:`StarredElement` nodes
    #: in the tuple.
    elements: Sequence[BaseElement]

    lpar: Sequence[LeftParen] = field(default_factory=lambda: (LeftParen(),))
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = field(default_factory=lambda: (RightParen(),))

    def _safe_to_use_with_word_operator(self, position: ExpressionPosition) -> bool:
        if super(Tuple, self)._safe_to_use_with_word_operator(position):
            # if we have parenthesis, we're safe.
            return True
        # elements[-1] and elements[0] must exist past this point, because
        # we're not parenthesized, meaning we must have at least one element.
        elements = self.elements
        if position == ExpressionPosition.LEFT:
            last_element = elements[-1]
            return (
                isinstance(last_element.comma, Comma)
                or (
                    isinstance(last_element, StarredElement)
                    and len(last_element.rpar) > 0
                )
                or last_element.value._safe_to_use_with_word_operator(position)
            )
        else:  # ExpressionPosition.RIGHT
            first_element = elements[0]
            # starred elements are always safe because they begin with ( or *
            return isinstance(
                first_element, StarredElement
            ) or first_element.value._safe_to_use_with_word_operator(position)

    def _validate(self) -> None:
        # Paren validation and such
        super(Tuple, self)._validate()

        if len(self.elements) == 0:
            if len(self.lpar) == 0:  # assumes len(lpar) == len(rpar), via superclass
                raise CSTValidationError(
                    "A zero-length tuple must be wrapped in parentheses."
                )
        # Invalid commas aren't possible, because MaybeSentinel will ensure that there
        # is a comma where required.

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Tuple":
        return Tuple(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            elements=visit_sequence(self, "elements", self.elements, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state):
            elements = self.elements
            if len(elements) == 1:
                elements[0]._codegen(
                    state, default_comma=True, default_comma_whitespace=False
                )
            else:
                for idx, el in enumerate(elements):
                    el._codegen(
                        state,
                        default_comma=(idx < len(elements) - 1),
                        default_comma_whitespace=True,
                    )


class BaseList(BaseExpression, ABC):
    """
    A base class for :class:`List` and :class:`ListComp`, which both result in a list
    object when evaluated.
    """

    __slots__ = ()

    lbracket: LeftSquareBracket = LeftSquareBracket.field()
    #: Brackets surrounding the list.
    rbracket: RightSquareBracket = RightSquareBracket.field()

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    def _safe_to_use_with_word_operator(self, position: ExpressionPosition) -> bool:
        return True

    @contextmanager
    def _bracketize(self, state: CodegenState) -> Generator[None, None, None]:
        self.lbracket._codegen(state)
        yield
        self.rbracket._codegen(state)


@add_slots
@dataclass(frozen=True)
class List(BaseList, BaseAssignTargetExpression, BaseDelTargetExpression):
    """
    A mutable literal list.

    ::

        List([
            Element(Integer("1")),
            Element(Integer("2")),
            StarredElement(Name("others")),
        ])

    generates the following code::

        [1, 2, *others]

    List comprehensions are represented with a :class:`ListComp` node.
    """

    #: A sequence containing all the :class:`Element` and :class:`StarredElement` nodes
    #: in the list.
    elements: Sequence[BaseElement]

    lbracket: LeftSquareBracket = LeftSquareBracket.field()
    #: Brackets surrounding the list.
    rbracket: RightSquareBracket = RightSquareBracket.field()

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "List":
        return List(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            lbracket=visit_required(self, "lbracket", self.lbracket, visitor),
            elements=visit_sequence(self, "elements", self.elements, visitor),
            rbracket=visit_required(self, "rbracket", self.rbracket, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state), self._bracketize(state):
            elements = self.elements
            for idx, el in enumerate(elements):
                el._codegen(
                    state,
                    default_comma=(idx < len(elements) - 1),
                    default_comma_whitespace=True,
                )


class _BaseSetOrDict(BaseExpression, ABC):
    """
    An abstract base class for :class:`BaseSet` and :class:`BaseDict`.

    Literal sets and dicts are syntactically similar (hence this shared base class), but
    are semantically different. This base class is an implementation detail and
    shouldn't be exported.
    """

    __slots__ = ()

    lbrace: LeftCurlyBrace = LeftCurlyBrace.field()
    #: Braces surrounding the set or dict.
    rbrace: RightCurlyBrace = RightCurlyBrace.field()

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    def _safe_to_use_with_word_operator(self, position: ExpressionPosition) -> bool:
        return True

    # brace-ize seems like a very made-up word. And it is!
    @contextmanager
    def _braceize(self, state: CodegenState) -> Generator[None, None, None]:
        self.lbrace._codegen(state)
        yield
        self.rbrace._codegen(state)


class BaseSet(_BaseSetOrDict, ABC):
    """
    An abstract base class for :class:`Set` and :class:`SetComp`, which both result in
    a set object when evaluated.
    """

    __slots__ = ()


@add_slots
@dataclass(frozen=True)
class Set(BaseSet):
    """
    A mutable literal set.

    ::

        Set([
            Element(Integer("1")),
            Element(Integer("2")),
            StarredElement(Name("others")),
        ])

    generates the following code::

        {1, 2, *others}

    Set comprehensions are represented with a :class:`SetComp` node.
    """

    #: A sequence containing all the :class:`Element` and :class:`StarredElement` nodes
    #: in the set.
    elements: Sequence[BaseElement]

    lbrace: LeftCurlyBrace = LeftCurlyBrace.field()
    #: Braces surrounding the set.
    rbrace: RightCurlyBrace = RightCurlyBrace.field()

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    def _validate(self) -> None:
        super(Set, self)._validate()

        if len(self.elements) == 0:
            raise CSTValidationError(
                "A literal set must have at least one element. A zero-element set "
                + "would be syntatically ambiguous with an empty dict, `{}`."
            )

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Set":
        return Set(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            lbrace=visit_required(self, "lbrace", self.lbrace, visitor),
            elements=visit_sequence(self, "elements", self.elements, visitor),
            rbrace=visit_required(self, "rbrace", self.rbrace, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state), self._braceize(state):
            elements = self.elements
            for idx, el in enumerate(elements):
                el._codegen(
                    state,
                    default_comma=(idx < len(elements) - 1),
                    default_comma_whitespace=True,
                )


class BaseDict(_BaseSetOrDict, ABC):
    """
    An abstract base class for :class:`Dict` and :class:`DictComp`, which both result in
    a dict object when evaluated.
    """

    __slots__ = ()


@add_slots
@dataclass(frozen=True)
class Dict(BaseDict):
    """
    A literal dictionary. Key-value pairs are stored in ``elements`` using
    :class:`DictElement` nodes.

    It's possible to expand one dictionary into another, as in ``{k: v, **expanded}``.
    Expanded elements are stored as :class:`StarredDictElement` nodes.

    ::

        Dict([
            DictElement(Name("k1"), Name("v1")),
            DictElement(Name("k2"), Name("v2")),
            StarredDictElement(Name("expanded")),
        ])

    generates the following code::

        {k1: v1, k2: v2, **expanded}
    """

    elements: Sequence[BaseDictElement]
    lbrace: LeftCurlyBrace = LeftCurlyBrace.field()
    rbrace: RightCurlyBrace = RightCurlyBrace.field()
    lpar: Sequence[LeftParen] = ()
    rpar: Sequence[RightParen] = ()

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Dict":
        return Dict(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            lbrace=visit_required(self, "lbrace", self.lbrace, visitor),
            elements=visit_sequence(self, "elements", self.elements, visitor),
            rbrace=visit_required(self, "rbrace", self.rbrace, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state), self._braceize(state):
            elements = self.elements
            for idx, el in enumerate(elements):
                el._codegen(
                    state,
                    default_comma=(idx < len(elements) - 1),
                    default_comma_whitespace=True,
                )


@add_slots
@dataclass(frozen=True)
class CompFor(CSTNode):
    """
    One ``for`` clause in a :class:`BaseComp`, or a nested hierarchy of
    ``for`` clauses.

    Nested loops in comprehensions are difficult to get right, but they can be thought
    of as a flat representation of nested clauses.

    ``elt for a in b for c in d if e`` can be thought of as::

        for a in b:
            for c in d:
                if e:
                    yield elt

    And that would form the following CST::

        ListComp(
            elt=Name("elt"),
            for_in=CompFor(
                target=Name("a"),
                iter=Name("b"),
                ifs=[],
                inner_comp_for=CompFor(
                    target=Name("c"),
                    iter=Name("d"),
                    ifs=[
                        CompIf(
                            test=Name("e"),
                        ),
                    ],
                ),
            ),
        )

    Normal ``for`` statements are provided by :class:`For`.
    """

    #: The target to assign a value to in each iteration of the loop. This is different
    #: from :attr:`GeneratorExp.elt`, :attr:`ListComp.elt`, :attr:`SetComp.elt`, and
    #: ``key`` and ``value`` in :class:`DictComp`, because it doesn't directly effect
    #: the value of resulting generator, list, set, or dict.
    target: BaseAssignTargetExpression

    #: The value to iterate over. Every value in ``iter`` is stored in ``target``.
    iter: BaseExpression

    #: Zero or more conditional clauses that control this loop. If any of these tests
    #: fail, the ``target`` item is skipped.
    #:
    #: ::
    #:
    #:     if a if b if c
    #:
    #: has similar semantics to::
    #:
    #:     if a and b and c
    ifs: Sequence["CompIf"] = ()

    #: Another :class:`CompFor` node used to form nested loops. Nested comprehensions
    #: can be useful, but they tend to be difficult to read and write. As a result they
    #: are uncommon.
    inner_for_in: Optional["CompFor"] = None

    #: An optional async modifier that appears before the ``for`` keyword.
    asynchronous: Optional[Asynchronous] = None

    #: Whitespace that appears at the beginning of this node, before the ``for`` and
    #: ``async`` keywords.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Whitespace appearing after the ``for`` keyword, but before the ``target``.
    whitespace_after_for: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Whitespace appearing after the ``target``, but before the ``in`` keyword.
    whitespace_before_in: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Whitespace appearing after the ``in`` keyword, but before the ``iter``.
    whitespace_after_in: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _validate(self) -> None:
        if (
            self.whitespace_after_for.empty
            and not self.target._safe_to_use_with_word_operator(
                ExpressionPosition.RIGHT
            )
        ):
            raise CSTValidationError(
                "Must have at least one space after 'for' keyword."
            )

        if (
            self.whitespace_before_in.empty
            and not self.target._safe_to_use_with_word_operator(ExpressionPosition.LEFT)
        ):
            raise CSTValidationError(
                "Must have at least one space before 'in' keyword."
            )

        if (
            self.whitespace_after_in.empty
            and not self.iter._safe_to_use_with_word_operator(ExpressionPosition.RIGHT)
        ):
            raise CSTValidationError("Must have at least one space after 'in' keyword.")

        prev_expr = self.iter
        for if_clause in self.ifs:
            if (
                if_clause.whitespace_before.empty
                and not prev_expr._safe_to_use_with_word_operator(
                    ExpressionPosition.LEFT
                )
            ):
                raise CSTValidationError(
                    "Must have at least one space before 'if' keyword."
                )
            prev_expr = if_clause.test

        inner_for_in = self.inner_for_in
        if (
            inner_for_in is not None
            and inner_for_in.whitespace_before.empty
            and not prev_expr._safe_to_use_with_word_operator(ExpressionPosition.LEFT)
        ):
            keyword = "async" if inner_for_in.asynchronous else "for"
            raise CSTValidationError(
                f"Must have at least one space before '{keyword}' keyword."
            )

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "CompFor":
        return CompFor(
            whitespace_before=visit_required(
                self, "whitespace_before", self.whitespace_before, visitor
            ),
            asynchronous=visit_optional(
                self, "asynchronous", self.asynchronous, visitor
            ),
            whitespace_after_for=visit_required(
                self, "whitespace_after_for", self.whitespace_after_for, visitor
            ),
            target=visit_required(self, "target", self.target, visitor),
            whitespace_before_in=visit_required(
                self, "whitespace_before_in", self.whitespace_before_in, visitor
            ),
            whitespace_after_in=visit_required(
                self, "whitespace_after_in", self.whitespace_after_in, visitor
            ),
            iter=visit_required(self, "iter", self.iter, visitor),
            ifs=visit_sequence(self, "ifs", self.ifs, visitor),
            inner_for_in=visit_optional(
                self, "inner_for_in", self.inner_for_in, visitor
            ),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        self.whitespace_before._codegen(state)
        asynchronous = self.asynchronous
        if asynchronous is not None:
            asynchronous._codegen(state)
        state.add_token("for")
        self.whitespace_after_for._codegen(state)
        self.target._codegen(state)
        self.whitespace_before_in._codegen(state)
        state.add_token("in")
        self.whitespace_after_in._codegen(state)
        self.iter._codegen(state)
        ifs = self.ifs
        for if_clause in ifs:
            if_clause._codegen(state)
        inner_for_in = self.inner_for_in
        if inner_for_in is not None:
            inner_for_in._codegen(state)


@add_slots
@dataclass(frozen=True)
class CompIf(CSTNode):
    """
    A conditional clause in a :class:`CompFor`, used as part of a generator or
    comprehension expression.

    If the ``test`` fails, the current element in the :class:`CompFor` will be skipped.
    """

    #: An expression to evaluate. When interpreted, Python will coerce it to a boolean.
    test: BaseExpression

    #: Whitespace before the ``if`` keyword.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Whitespace after the ``if`` keyword, but before the ``test`` expression.
    whitespace_before_test: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _validate(self) -> None:
        if (
            self.whitespace_before_test.empty
            and not self.test._safe_to_use_with_word_operator(ExpressionPosition.RIGHT)
        ):
            raise CSTValidationError("Must have at least one space after 'if' keyword.")

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "CompIf":
        return CompIf(
            whitespace_before=visit_required(
                self, "whitespace_before", self.whitespace_before, visitor
            ),
            whitespace_before_test=visit_required(
                self, "whitespace_before_test", self.whitespace_before_test, visitor
            ),
            test=visit_required(self, "test", self.test, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        self.whitespace_before._codegen(state)
        state.add_token("if")
        self.whitespace_before_test._codegen(state)
        self.test._codegen(state)


class BaseComp(BaseExpression, ABC):
    """
    A base class for all comprehension and generator expressions, including
    :class:`GeneratorExp`, :class:`ListComp`, :class:`SetComp`, and :class:`DictComp`.
    """

    __slots__ = ()

    for_in: CompFor


class BaseSimpleComp(BaseComp, ABC):
    """
    The base class for :class:`ListComp`, :class:`SetComp`, and :class:`GeneratorExp`.
    :class:`DictComp` is not a :class:`BaseSimpleComp`, because it uses ``key`` and
    ``value``.
    """

    __slots__ = ()

    #: The expression evaluated during each iteration of the comprehension. This
    #: lexically comes before the ``for_in`` clause, but it is semantically the
    #: inner-most element, evaluated inside the ``for_in`` clause.
    elt: BaseExpression

    #: The ``for ... in ... if ...`` clause that lexically comes after ``elt``. This may
    #: be a nested structure for nested comprehensions. See :class:`CompFor` for
    #: details.
    for_in: CompFor

    def _validate(self) -> None:
        super(BaseSimpleComp, self)._validate()

        for_in = self.for_in
        if (
            for_in.whitespace_before.empty
            and not self.elt._safe_to_use_with_word_operator(ExpressionPosition.LEFT)
        ):
            keyword = "async" if for_in.asynchronous else "for"
            raise CSTValidationError(
                f"Must have at least one space before '{keyword}' keyword."
            )


@add_slots
@dataclass(frozen=True)
class GeneratorExp(BaseSimpleComp):
    """
    A generator expression. ``elt`` represents the value yielded for each item in
    :attr:`CompFor.iter`.

    All ``for ... in ...`` and ``if ...`` clauses are stored as a recursive
    :class:`CompFor` data structure inside ``for_in``.
    """

    #: The expression evaluated and yielded during each iteration of the generator.
    elt: BaseExpression

    #: The ``for ... in ... if ...`` clause that comes after ``elt``. This may be a
    #: nested structure for nested comprehensions. See :class:`CompFor` for details.
    for_in: CompFor

    lpar: Sequence[LeftParen] = field(default_factory=lambda: (LeftParen(),))
    #: Sequence of parentheses for precedence dictation. Generator expressions must
    #: always be parenthesized. However, if a generator expression is the only argument
    #: inside a function call, the enclosing :class:`Call` node may own the parentheses
    #: instead.
    rpar: Sequence[RightParen] = field(default_factory=lambda: (RightParen(),))

    def _safe_to_use_with_word_operator(self, position: ExpressionPosition) -> bool:
        # Generators are always parenthesized
        return True

    # A note about validation: Generators must always be parenthesized, but it's
    # possible that this Generator node doesn't own those parenthesis (in the case of a
    # function call with a single generator argument).
    #
    # Therefore, there's no useful validation we can do here. In theory, our parent
    # could do the validation, but there's a ton of potential parents to a Generator, so
    # it's not worth the effort.

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "GeneratorExp":
        return GeneratorExp(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            elt=visit_required(self, "elt", self.elt, visitor),
            for_in=visit_required(self, "for_in", self.for_in, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state):
            self.elt._codegen(state)
            self.for_in._codegen(state)


@add_slots
@dataclass(frozen=True)
class ListComp(BaseList, BaseSimpleComp):
    """
    A list comprehension. ``elt`` represents the value stored for each item in
    :attr:`CompFor.iter`.

    All ``for ... in ...`` and ``if ...`` clauses are stored as a recursive
    :class:`CompFor` data structure inside ``for_in``.
    """

    #: The expression evaluated and stored during each iteration of the comprehension.
    elt: BaseExpression

    #: The ``for ... in ... if ...`` clause that comes after ``elt``. This may be a
    #: nested structure for nested comprehensions. See :class:`CompFor` for details.
    for_in: CompFor

    lbracket: LeftSquareBracket = LeftSquareBracket.field()
    #: Brackets surrounding the list comprehension.
    rbracket: RightSquareBracket = RightSquareBracket.field()

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "ListComp":
        return ListComp(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            lbracket=visit_required(self, "lbracket", self.lbracket, visitor),
            elt=visit_required(self, "elt", self.elt, visitor),
            for_in=visit_required(self, "for_in", self.for_in, visitor),
            rbracket=visit_required(self, "rbracket", self.rbracket, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state), self._bracketize(state):
            self.elt._codegen(state)
            self.for_in._codegen(state)


@add_slots
@dataclass(frozen=True)
class SetComp(BaseSet, BaseSimpleComp):
    """
    A set comprehension. ``elt`` represents the value stored for each item in
    :attr:`CompFor.iter`.

    All ``for ... in ...`` and ``if ...`` clauses are stored as a recursive
    :class:`CompFor` data structure inside ``for_in``.
    """

    #: The expression evaluated and stored during each iteration of the comprehension.
    elt: BaseExpression

    #: The ``for ... in ... if ...`` clause that comes after ``elt``. This may be a
    #: nested structure for nested comprehensions. See :class:`CompFor` for details.
    for_in: CompFor

    lbrace: LeftCurlyBrace = LeftCurlyBrace.field()
    #: Braces surrounding the set comprehension.
    rbrace: RightCurlyBrace = RightCurlyBrace.field()

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "SetComp":
        return SetComp(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            lbrace=visit_required(self, "lbrace", self.lbrace, visitor),
            elt=visit_required(self, "elt", self.elt, visitor),
            for_in=visit_required(self, "for_in", self.for_in, visitor),
            rbrace=visit_required(self, "rbrace", self.rbrace, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state), self._braceize(state):
            self.elt._codegen(state)
            self.for_in._codegen(state)


@add_slots
@dataclass(frozen=True)
class DictComp(BaseDict, BaseComp):
    """
    A dictionary comprehension. ``key`` and ``value`` represent the dictionary entry
    evaluated for each item.

    All ``for ... in ...`` and ``if ...`` clauses are stored as a recursive
    :class:`CompFor` data structure inside ``for_in``.
    """

    #: The key inserted into the dictionary during each iteration of the comprehension.
    key: BaseExpression
    #: The value associated with the ``key`` inserted into the dictionary during each
    #: iteration of the comprehension.
    value: BaseExpression

    #: The ``for ... in ... if ...`` clause that lexically comes after ``key`` and
    #: ``value``. This may be a nested structure for nested comprehensions. See
    #: :class:`CompFor` for details.
    for_in: CompFor

    lbrace: LeftCurlyBrace = LeftCurlyBrace.field()
    #: Braces surrounding the dict comprehension.
    rbrace: RightCurlyBrace = RightCurlyBrace.field()

    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    #: Whitespace after the key, but before the colon in ``key : value``.
    whitespace_before_colon: BaseParenthesizableWhitespace = SimpleWhitespace.field("")
    #: Whitespace after the colon, but before the value in ``key : value``.
    whitespace_after_colon: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _validate(self) -> None:
        super(DictComp, self)._validate()

        for_in = self.for_in
        if (
            for_in.whitespace_before.empty
            and not self.value._safe_to_use_with_word_operator(ExpressionPosition.LEFT)
        ):
            keyword = "async" if for_in.asynchronous else "for"
            raise CSTValidationError(
                f"Must have at least one space before '{keyword}' keyword."
            )

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "DictComp":
        return DictComp(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            lbrace=visit_required(self, "lbrace", self.lbrace, visitor),
            key=visit_required(self, "key", self.key, visitor),
            whitespace_before_colon=visit_required(
                self, "whitespace_before_colon", self.whitespace_before_colon, visitor
            ),
            whitespace_after_colon=visit_required(
                self, "whitespace_after_colon", self.whitespace_after_colon, visitor
            ),
            value=visit_required(self, "value", self.value, visitor),
            for_in=visit_required(self, "for_in", self.for_in, visitor),
            rbrace=visit_required(self, "rbrace", self.rbrace, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state), self._braceize(state):
            self.key._codegen(state)
            self.whitespace_before_colon._codegen(state)
            state.add_token(":")
            self.whitespace_after_colon._codegen(state)
            self.value._codegen(state)
            self.for_in._codegen(state)


@add_slots
@dataclass(frozen=True)
class NamedExpr(BaseExpression):
    """
    An expression that is also an assignment, such as ``x := y + z``. Affectionately
    known as the walrus operator, this expression allows you to make an assignment
    inside an expression. This greatly simplifies loops::

        while line := read_some_line_or_none():
            do_thing_with_line(line)
    """

    #: The target that is being assigned to.
    target: BaseExpression

    #: The expression being assigned to the target.
    value: BaseExpression

    #: Sequence of parenthesis for precedence dictation.
    lpar: Sequence[LeftParen] = ()
    #: Sequence of parenthesis for precedence dictation.
    rpar: Sequence[RightParen] = ()

    #: Whitespace after the target, but before the walrus operator.
    whitespace_before_walrus: BaseParenthesizableWhitespace = SimpleWhitespace.field(
        " "
    )
    #: Whitespace after the walrus operator, but before the value.
    whitespace_after_walrus: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "NamedExpr":
        return NamedExpr(
            lpar=visit_sequence(self, "lpar", self.lpar, visitor),
            target=visit_required(self, "target", self.target, visitor),
            whitespace_before_walrus=visit_required(
                self, "whitespace_before_walrus", self.whitespace_before_walrus, visitor
            ),
            whitespace_after_walrus=visit_required(
                self, "whitespace_after_walrus", self.whitespace_after_walrus, visitor
            ),
            value=visit_required(self, "value", self.value, visitor),
            rpar=visit_sequence(self, "rpar", self.rpar, visitor),
        )

    def _safe_to_use_with_word_operator(self, position: ExpressionPosition) -> bool:
        if position == ExpressionPosition.LEFT:
            return len(self.rpar) > 0 or self.value._safe_to_use_with_word_operator(
                position
            )
        return len(self.lpar) > 0 or self.target._safe_to_use_with_word_operator(
            position
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        with self._parenthesize(state):
            self.target._codegen(state)
            self.whitespace_before_walrus._codegen(state)
            state.add_token(":=")
            self.whitespace_after_walrus._codegen(state)
            self.value._codegen(state)
