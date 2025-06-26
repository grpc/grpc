# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


from dataclasses import dataclass
from typing import Generic, Optional, Sequence, TypeVar, Union

from libcst._add_slots import add_slots
from libcst._nodes.expression import (
    Annotation,
    Arg,
    Attribute,
    BaseExpression,
    BaseFormattedStringContent,
    Index,
    LeftParen,
    LeftSquareBracket,
    Name,
    Parameters,
    RightParen,
    RightSquareBracket,
    Slice,
    SubscriptElement,
)
from libcst._nodes.op import AssignEqual, BaseAugOp, Colon, Dot
from libcst._nodes.statement import AsName, BaseSmallStatement, Decorator, ImportAlias
from libcst._nodes.whitespace import EmptyLine, SimpleWhitespace, TrailingWhitespace
from libcst._parser.types.whitespace_state import WhitespaceState

_T = TypeVar("_T")


@add_slots
@dataclass(frozen=True)
class WithLeadingWhitespace(Generic[_T]):
    value: _T
    whitespace_before: WhitespaceState


@add_slots
@dataclass(frozen=True)
class SimpleStatementPartial:
    body: Sequence[BaseSmallStatement]
    whitespace_before: WhitespaceState
    trailing_whitespace: TrailingWhitespace


@add_slots
@dataclass(frozen=True)
class SlicePartial:
    second_colon: Colon
    step: Optional[BaseExpression]


@add_slots
@dataclass(frozen=True)
class AttributePartial:
    dot: Dot
    attr: Name


@add_slots
@dataclass(frozen=True)
class ArglistPartial:
    args: Sequence[Arg]


@add_slots
@dataclass(frozen=True)
class CallPartial:
    lpar: WithLeadingWhitespace[LeftParen]
    args: Sequence[Arg]
    rpar: RightParen


@add_slots
@dataclass(frozen=True)
class SubscriptPartial:
    slice: Union[Index, Slice, Sequence[SubscriptElement]]
    lbracket: LeftSquareBracket
    rbracket: RightSquareBracket
    whitespace_before: WhitespaceState


@add_slots
@dataclass(frozen=True)
class AnnAssignPartial:
    annotation: Annotation
    equal: Optional[AssignEqual]
    value: Optional[BaseExpression]


@add_slots
@dataclass(frozen=True)
class AugAssignPartial:
    operator: BaseAugOp
    value: BaseExpression


@add_slots
@dataclass(frozen=True)
class AssignPartial:
    equal: AssignEqual
    value: BaseExpression


class ParamStarPartial:
    pass


@add_slots
@dataclass(frozen=True)
class FuncdefPartial:
    lpar: LeftParen
    params: Parameters
    rpar: RightParen


@add_slots
@dataclass(frozen=True)
class DecoratorPartial:
    decorators: Sequence[Decorator]


@add_slots
@dataclass(frozen=True)
class ImportPartial:
    names: Sequence[ImportAlias]


@add_slots
@dataclass(frozen=True)
class ImportRelativePartial:
    relative: Sequence[Dot]
    module: Optional[Union[Attribute, Name]]


@add_slots
@dataclass(frozen=True)
class FormattedStringConversionPartial:
    value: str
    whitespace_before: WhitespaceState


@add_slots
@dataclass(frozen=True)
class FormattedStringFormatSpecPartial:
    values: Sequence[BaseFormattedStringContent]
    whitespace_before: WhitespaceState


@add_slots
@dataclass(frozen=True)
class ExceptClausePartial:
    leading_lines: Sequence[EmptyLine]
    whitespace_after_except: SimpleWhitespace
    type: Optional[BaseExpression] = None
    name: Optional[AsName] = None
