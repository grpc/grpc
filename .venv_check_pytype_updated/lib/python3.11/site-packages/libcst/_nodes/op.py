# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from abc import ABC, abstractmethod
from dataclasses import dataclass
from typing import Tuple

from libcst._add_slots import add_slots
from libcst._nodes.base import BaseLeaf, CSTNode, CSTValidationError
from libcst._nodes.internal import CodegenState, visit_required
from libcst._nodes.whitespace import BaseParenthesizableWhitespace, SimpleWhitespace
from libcst._visitors import CSTVisitorT


class _BaseOneTokenOp(CSTNode, ABC):
    """
    Any node that has a static value and needs to own whitespace on both sides.
    """

    __slots__ = ()

    whitespace_before: BaseParenthesizableWhitespace

    whitespace_after: BaseParenthesizableWhitespace

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "_BaseOneTokenOp":
        # pyre-ignore Pyre thinks that self.__class__ is CSTNode, not _BaseOneTokenOp
        return self.__class__(
            whitespace_before=visit_required(
                self, "whitespace_before", self.whitespace_before, visitor
            ),
            whitespace_after=visit_required(
                self, "whitespace_after", self.whitespace_after, visitor
            ),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        self.whitespace_before._codegen(state)
        with state.record_syntactic_position(self):
            state.add_token(self._get_token())
        self.whitespace_after._codegen(state)

    @abstractmethod
    def _get_token(self) -> str: ...


class _BaseTwoTokenOp(CSTNode, ABC):
    """
    Any node that ends up as two tokens, so we must preserve the whitespace
    in beteween them.
    """

    __slots__ = ()

    whitespace_before: BaseParenthesizableWhitespace

    whitespace_between: BaseParenthesizableWhitespace

    whitespace_after: BaseParenthesizableWhitespace

    def _validate(self) -> None:
        if self.whitespace_between.empty:
            raise CSTValidationError("Must have at least one space between not and in.")

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "_BaseTwoTokenOp":
        # pyre-ignore Pyre thinks that self.__class__ is CSTNode, not _BaseTwoTokenOp
        return self.__class__(
            whitespace_before=visit_required(
                self, "whitespace_before", self.whitespace_before, visitor
            ),
            whitespace_between=visit_required(
                self, "whitespace_between", self.whitespace_between, visitor
            ),
            whitespace_after=visit_required(
                self, "whitespace_after", self.whitespace_after, visitor
            ),
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        self.whitespace_before._codegen(state)
        with state.record_syntactic_position(self):
            state.add_token(self._get_tokens()[0])
            self.whitespace_between._codegen(state)
            state.add_token(self._get_tokens()[1])
        self.whitespace_after._codegen(state)

    @abstractmethod
    def _get_tokens(self) -> Tuple[str, str]: ...


class BaseUnaryOp(CSTNode, ABC):
    """
    Any node that has a static value used in a :class:`UnaryOperation` expression.
    """

    __slots__ = ()

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "BaseUnaryOp":
        # pyre-ignore Pyre thinks that self.__class__ is CSTNode, not BaseUnaryOp
        return self.__class__(
            whitespace_after=visit_required(
                self, "whitespace_after", self.whitespace_after, visitor
            )
        )

    def _codegen_impl(self, state: CodegenState) -> None:
        state.add_token(self._get_token())
        self.whitespace_after._codegen(state)

    @abstractmethod
    def _get_token(self) -> str: ...


class BaseBooleanOp(_BaseOneTokenOp, ABC):
    """
    Any node that has a static value used in a :class:`BooleanOperation` expression.
    This node is purely for typing.
    """

    __slots__ = ()


class BaseBinaryOp(CSTNode, ABC):
    """
    Any node that has a static value used in a :class:`BinaryOperation` expression.
    This node is purely for typing.
    """

    __slots__ = ()


class BaseCompOp(CSTNode, ABC):
    """
    Any node that has a static value used in a :class:`Comparison` expression.
    This node is purely for typing.
    """

    __slots__ = ()


class BaseAugOp(CSTNode, ABC):
    """
    Any node that has a static value used in an :class:`AugAssign` assignment.
    This node is purely for typing.
    """

    __slots__ = ()


@add_slots
@dataclass(frozen=True)
class Semicolon(_BaseOneTokenOp):
    """
    Used by any small statement (any subclass of :class:`BaseSmallStatement`
    such as :class:`Pass`) as a separator between subsequent nodes contained
    within a :class:`SimpleStatementLine` or :class:`SimpleStatementSuite`.
    """

    #: Any space that appears directly before this semicolon.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    #: Any space that appears directly after this semicolon.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    def _get_token(self) -> str:
        return ";"


@add_slots
@dataclass(frozen=True)
class Colon(_BaseOneTokenOp):
    """
    Used by :class:`Slice` as a separator between subsequent expressions,
    and in :class:`Lambda` to separate arguments and body.
    """

    #: Any space that appears directly before this colon.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    #: Any space that appears directly after this colon.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    def _get_token(self) -> str:
        return ":"


@add_slots
@dataclass(frozen=True)
class Comma(_BaseOneTokenOp):
    """
    Syntactic trivia used as a separator between subsequent items in various
    parts of the grammar.

    Some use-cases are:

    * :class:`Import` or :class:`ImportFrom`.
    * :class:`FunctionDef` arguments.
    * :class:`Tuple`/:class:`List`/:class:`Set`/:class:`Dict` elements.
    """

    #: Any space that appears directly before this comma.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    #: Any space that appears directly after this comma.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    def _get_token(self) -> str:
        return ","


@add_slots
@dataclass(frozen=True)
class Dot(_BaseOneTokenOp):
    """
    Used by :class:`Attribute` as a separator between subsequent :class:`Name` nodes.
    """

    #: Any space that appears directly before this dot.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    #: Any space that appears directly after this dot.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    def _get_token(self) -> str:
        return "."


@add_slots
@dataclass(frozen=True)
class ImportStar(BaseLeaf):
    """
    Used by :class:`ImportFrom` to denote a star import instead of a list
    of importable objects.
    """

    def _codegen_impl(self, state: CodegenState) -> None:
        state.add_token("*")


@add_slots
@dataclass(frozen=True)
class AssignEqual(_BaseOneTokenOp):
    """
    Used by :class:`AnnAssign` to denote a single equal character when doing an
    assignment on top of a type annotation. Also used by :class:`Param` and
    :class:`Arg` to denote assignment of a default value, and by
    :class:`FormattedStringExpression` to denote usage of self-documenting
    expressions.
    """

    #: Any space that appears directly before this equal sign.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this equal sign.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "="


@add_slots
@dataclass(frozen=True)
class Plus(BaseUnaryOp):
    """
    A unary operator that can be used in a :class:`UnaryOperation`
    expression.
    """

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    def _get_token(self) -> str:
        return "+"


@add_slots
@dataclass(frozen=True)
class Minus(BaseUnaryOp):
    """
    A unary operator that can be used in a :class:`UnaryOperation`
    expression.
    """

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    def _get_token(self) -> str:
        return "-"


@add_slots
@dataclass(frozen=True)
class BitInvert(BaseUnaryOp):
    """
    A unary operator that can be used in a :class:`UnaryOperation`
    expression.
    """

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field("")

    def _get_token(self) -> str:
        return "~"


@add_slots
@dataclass(frozen=True)
class Not(BaseUnaryOp):
    """
    A unary operator that can be used in a :class:`UnaryOperation`
    expression.
    """

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "not"


@add_slots
@dataclass(frozen=True)
class And(BaseBooleanOp):
    """
    A boolean operator that can be used in a :class:`BooleanOperation`
    expression.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "and"


@add_slots
@dataclass(frozen=True)
class Or(BaseBooleanOp):
    """
    A boolean operator that can be used in a :class:`BooleanOperation`
    expression.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "or"


@add_slots
@dataclass(frozen=True)
class Add(BaseBinaryOp, _BaseOneTokenOp):
    """
    A binary operator that can be used in a :class:`BinaryOperation`
    expression.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "+"


@add_slots
@dataclass(frozen=True)
class Subtract(BaseBinaryOp, _BaseOneTokenOp):
    """
    A binary operator that can be used in a :class:`BinaryOperation`
    expression.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "-"


@add_slots
@dataclass(frozen=True)
class Multiply(BaseBinaryOp, _BaseOneTokenOp):
    """
    A binary operator that can be used in a :class:`BinaryOperation`
    expression.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "*"


@add_slots
@dataclass(frozen=True)
class Divide(BaseBinaryOp, _BaseOneTokenOp):
    """
    A binary operator that can be used in a :class:`BinaryOperation`
    expression.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "/"


@add_slots
@dataclass(frozen=True)
class FloorDivide(BaseBinaryOp, _BaseOneTokenOp):
    """
    A binary operator that can be used in a :class:`BinaryOperation`
    expression.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "//"


@add_slots
@dataclass(frozen=True)
class Modulo(BaseBinaryOp, _BaseOneTokenOp):
    """
    A binary operator that can be used in a :class:`BinaryOperation`
    expression.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "%"


@add_slots
@dataclass(frozen=True)
class Power(BaseBinaryOp, _BaseOneTokenOp):
    """
    A binary operator that can be used in a :class:`BinaryOperation`
    expression.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "**"


@add_slots
@dataclass(frozen=True)
class LeftShift(BaseBinaryOp, _BaseOneTokenOp):
    """
    A binary operator that can be used in a :class:`BinaryOperation`
    expression.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "<<"


@add_slots
@dataclass(frozen=True)
class RightShift(BaseBinaryOp, _BaseOneTokenOp):
    """
    A binary operator that can be used in a :class:`BinaryOperation`
    expression.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return ">>"


@add_slots
@dataclass(frozen=True)
class BitOr(BaseBinaryOp, _BaseOneTokenOp):
    """
    A binary operator that can be used in a :class:`BinaryOperation`
    expression.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "|"


@add_slots
@dataclass(frozen=True)
class BitAnd(BaseBinaryOp, _BaseOneTokenOp):
    """
    A binary operator that can be used in a :class:`BinaryOperation`
    expression.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "&"


@add_slots
@dataclass(frozen=True)
class BitXor(BaseBinaryOp, _BaseOneTokenOp):
    """
    A binary operator that can be used in a :class:`BinaryOperation`
    expression.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "^"


@add_slots
@dataclass(frozen=True)
class MatrixMultiply(BaseBinaryOp, _BaseOneTokenOp):
    """
    A binary operator that can be used in a :class:`BinaryOperation`
    expression.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "@"


@add_slots
@dataclass(frozen=True)
class LessThan(BaseCompOp, _BaseOneTokenOp):
    """
    A comparision operator that can be used in a :class:`Comparison` expression.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "<"


@add_slots
@dataclass(frozen=True)
class GreaterThan(BaseCompOp, _BaseOneTokenOp):
    """
    A comparision operator that can be used in a :class:`Comparison` expression.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return ">"


@add_slots
@dataclass(frozen=True)
class Equal(BaseCompOp, _BaseOneTokenOp):
    """
    A comparision operator that can be used in a :class:`Comparison` expression.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "=="


@add_slots
@dataclass(frozen=True)
class LessThanEqual(BaseCompOp, _BaseOneTokenOp):
    """
    A comparision operator that can be used in a :class:`Comparison` expression.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "<="


@add_slots
@dataclass(frozen=True)
class GreaterThanEqual(BaseCompOp, _BaseOneTokenOp):
    """
    A comparision operator that can be used in a :class:`Comparison` expression.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return ">="


@add_slots
@dataclass(frozen=True)
class NotEqual(BaseCompOp, _BaseOneTokenOp):
    """
    A comparison operator that can be used in a :class:`Comparison` expression.

    This node defines a static value for convenience, but in reality due to
    PEP 401 it can be one of two values, both of which should be a
    :class:`NotEqual` :class:`Comparison` operator.
    """

    #: The actual text value of this operator. Can be either ``!=`` or ``<>``.
    value: str = "!="

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _validate(self) -> None:
        if self.value not in ["!=", "<>"]:
            raise CSTValidationError("Invalid value for NotEqual node.")

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "NotEqual":
        return self.__class__(
            whitespace_before=visit_required(
                self, "whitespace_before", self.whitespace_before, visitor
            ),
            value=self.value,
            whitespace_after=visit_required(
                self, "whitespace_after", self.whitespace_after, visitor
            ),
        )

    def _get_token(self) -> str:
        return self.value


@add_slots
@dataclass(frozen=True)
class In(BaseCompOp, _BaseOneTokenOp):
    """
    A comparision operator that can be used in a :class:`Comparison` expression.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "in"


@add_slots
@dataclass(frozen=True)
class NotIn(BaseCompOp, _BaseTwoTokenOp):
    """
    A comparision operator that can be used in a :class:`Comparison` expression.

    This operator spans two tokens that must be separated by at least one space,
    so there is a third whitespace attribute to represent this.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears between the ``not`` and ``in`` tokens.
    whitespace_between: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_tokens(self) -> Tuple[str, str]:
        return ("not", "in")


@add_slots
@dataclass(frozen=True)
class Is(BaseCompOp, _BaseOneTokenOp):
    """
    A comparision operator that can be used in a :class:`Comparison` expression.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "is"


@add_slots
@dataclass(frozen=True)
class IsNot(BaseCompOp, _BaseTwoTokenOp):
    """
    A comparision operator that can be used in a :class:`Comparison` expression.

    This operator spans two tokens that must be separated by at least one space,
    so there is a third whitespace attribute to represent this.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears between the ``is`` and ``not`` tokens.
    whitespace_between: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_tokens(self) -> Tuple[str, str]:
        return ("is", "not")


@add_slots
@dataclass(frozen=True)
class AddAssign(BaseAugOp, _BaseOneTokenOp):
    """
    An augmented assignment operator that can be used in a :class:`AugAssign`
    statement.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "+="


@add_slots
@dataclass(frozen=True)
class SubtractAssign(BaseAugOp, _BaseOneTokenOp):
    """
    An augmented assignment operator that can be used in a :class:`AugAssign`
    statement.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "-="


@add_slots
@dataclass(frozen=True)
class MultiplyAssign(BaseAugOp, _BaseOneTokenOp):
    """
    An augmented assignment operator that can be used in a :class:`AugAssign`
    statement.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "*="


@add_slots
@dataclass(frozen=True)
class MatrixMultiplyAssign(BaseAugOp, _BaseOneTokenOp):
    """
    An augmented assignment operator that can be used in a :class:`AugAssign`
    statement.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "@="


@add_slots
@dataclass(frozen=True)
class DivideAssign(BaseAugOp, _BaseOneTokenOp):
    """
    An augmented assignment operator that can be used in a :class:`AugAssign`
    statement.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "/="


@add_slots
@dataclass(frozen=True)
class ModuloAssign(BaseAugOp, _BaseOneTokenOp):
    """
    An augmented assignment operator that can be used in a :class:`AugAssign`
    statement.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "%="


@add_slots
@dataclass(frozen=True)
class BitAndAssign(BaseAugOp, _BaseOneTokenOp):
    """
    An augmented assignment operator that can be used in a :class:`AugAssign`
    statement.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "&="


@add_slots
@dataclass(frozen=True)
class BitOrAssign(BaseAugOp, _BaseOneTokenOp):
    """
    An augmented assignment operator that can be used in a :class:`AugAssign`
    statement.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "|="


@add_slots
@dataclass(frozen=True)
class BitXorAssign(BaseAugOp, _BaseOneTokenOp):
    """
    An augmented assignment operator that can be used in a :class:`AugAssign`
    statement.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "^="


@add_slots
@dataclass(frozen=True)
class LeftShiftAssign(BaseAugOp, _BaseOneTokenOp):
    """
    An augmented assignment operator that can be used in a :class:`AugAssign`
    statement.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "<<="


@add_slots
@dataclass(frozen=True)
class RightShiftAssign(BaseAugOp, _BaseOneTokenOp):
    """
    An augmented assignment operator that can be used in a :class:`AugAssign`
    statement.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return ">>="


@add_slots
@dataclass(frozen=True)
class PowerAssign(BaseAugOp, _BaseOneTokenOp):
    """
    An augmented assignment operator that can be used in a :class:`AugAssign`
    statement.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "**="


@add_slots
@dataclass(frozen=True)
class FloorDivideAssign(BaseAugOp, _BaseOneTokenOp):
    """
    An augmented assignment operator that can be used in a :class:`AugAssign`
    statement.
    """

    #: Any space that appears directly before this operator.
    whitespace_before: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    #: Any space that appears directly after this operator.
    whitespace_after: BaseParenthesizableWhitespace = SimpleWhitespace.field(" ")

    def _get_token(self) -> str:
        return "//="
