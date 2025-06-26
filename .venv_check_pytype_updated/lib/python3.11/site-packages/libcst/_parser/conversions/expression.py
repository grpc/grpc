# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
# pyre-unsafe

import re
import typing
from tokenize import (
    Floatnumber as FLOATNUMBER_RE,
    Imagnumber as IMAGNUMBER_RE,
    Intnumber as INTNUMBER_RE,
)

from libcst import CSTLogicError
from libcst._exceptions import ParserSyntaxError, PartialParserSyntaxError
from libcst._maybe_sentinel import MaybeSentinel
from libcst._nodes.expression import (
    Arg,
    Asynchronous,
    Attribute,
    Await,
    BinaryOperation,
    BooleanOperation,
    Call,
    Comparison,
    ComparisonTarget,
    CompFor,
    CompIf,
    ConcatenatedString,
    Dict,
    DictComp,
    DictElement,
    Element,
    Ellipsis,
    Float,
    FormattedString,
    FormattedStringExpression,
    FormattedStringText,
    From,
    GeneratorExp,
    IfExp,
    Imaginary,
    Index,
    Integer,
    Lambda,
    LeftCurlyBrace,
    LeftParen,
    LeftSquareBracket,
    List,
    ListComp,
    Name,
    NamedExpr,
    Param,
    Parameters,
    RightCurlyBrace,
    RightParen,
    RightSquareBracket,
    Set,
    SetComp,
    Slice,
    StarredDictElement,
    StarredElement,
    Subscript,
    SubscriptElement,
    Tuple,
    UnaryOperation,
    Yield,
)
from libcst._nodes.op import (
    Add,
    And,
    AssignEqual,
    BaseBinaryOp,
    BaseBooleanOp,
    BaseCompOp,
    BitAnd,
    BitInvert,
    BitOr,
    BitXor,
    Colon,
    Comma,
    Divide,
    Dot,
    Equal,
    FloorDivide,
    GreaterThan,
    GreaterThanEqual,
    In,
    Is,
    IsNot,
    LeftShift,
    LessThan,
    LessThanEqual,
    MatrixMultiply,
    Minus,
    Modulo,
    Multiply,
    Not,
    NotEqual,
    NotIn,
    Or,
    Plus,
    Power,
    RightShift,
    Subtract,
)
from libcst._nodes.whitespace import SimpleWhitespace
from libcst._parser.custom_itertools import grouper
from libcst._parser.production_decorator import with_production
from libcst._parser.types.config import ParserConfig
from libcst._parser.types.partials import (
    ArglistPartial,
    AttributePartial,
    CallPartial,
    FormattedStringConversionPartial,
    FormattedStringFormatSpecPartial,
    SlicePartial,
    SubscriptPartial,
    WithLeadingWhitespace,
)
from libcst._parser.types.token import Token
from libcst._parser.whitespace_parser import parse_parenthesizable_whitespace

BINOP_TOKEN_LUT: typing.Dict[str, typing.Type[BaseBinaryOp]] = {
    "*": Multiply,
    "@": MatrixMultiply,
    "/": Divide,
    "%": Modulo,
    "//": FloorDivide,
    "+": Add,
    "-": Subtract,
    "<<": LeftShift,
    ">>": RightShift,
    "&": BitAnd,
    "^": BitXor,
    "|": BitOr,
}


BOOLOP_TOKEN_LUT: typing.Dict[str, typing.Type[BaseBooleanOp]] = {"and": And, "or": Or}


COMPOP_TOKEN_LUT: typing.Dict[str, typing.Type[BaseCompOp]] = {
    "<": LessThan,
    ">": GreaterThan,
    "==": Equal,
    "<=": LessThanEqual,
    ">=": GreaterThanEqual,
    "in": In,
    "is": Is,
}


# N.B. This uses a `testlist | star_expr`, not a `testlist_star_expr` because
# `testlist_star_expr` may not always be representable by a non-partial node, since it's
# only used as part of `expr_stmt`.
@with_production("expression_input", "(testlist | star_expr) ENDMARKER")
def convert_expression_input(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    (child, endmarker) = children
    # HACK: UGLY! REMOVE THIS SOON!
    # Unwrap WithLeadingWhitespace if it exists. It shouldn't exist by this point, but
    # testlist isn't fully implemented, and we currently leak these partial objects.
    if isinstance(child, WithLeadingWhitespace):
        child = child.value
    return child


@with_production("namedexpr_test", "test [':=' test]", version=">=3.8")
def convert_namedexpr_test(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    test, *assignment = children
    if len(assignment) == 0:
        return test

    # Convert all of the operations that have no precedence in a loop
    (walrus, value) = assignment
    return WithLeadingWhitespace(
        NamedExpr(
            target=test.value,
            whitespace_before_walrus=parse_parenthesizable_whitespace(
                config, walrus.whitespace_before
            ),
            whitespace_after_walrus=parse_parenthesizable_whitespace(
                config, walrus.whitespace_after
            ),
            value=value.value,
        ),
        test.whitespace_before,
    )


@with_production("test", "or_test ['if' or_test 'else' test] | lambdef")
def convert_test(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    if len(children) == 1:
        (child,) = children
        return child
    else:
        (body, if_token, test, else_token, orelse) = children
        return WithLeadingWhitespace(
            IfExp(
                body=body.value,
                test=test.value,
                orelse=orelse.value,
                whitespace_before_if=parse_parenthesizable_whitespace(
                    config, if_token.whitespace_before
                ),
                whitespace_after_if=parse_parenthesizable_whitespace(
                    config, if_token.whitespace_after
                ),
                whitespace_before_else=parse_parenthesizable_whitespace(
                    config, else_token.whitespace_before
                ),
                whitespace_after_else=parse_parenthesizable_whitespace(
                    config, else_token.whitespace_after
                ),
            ),
            body.whitespace_before,
        )


@with_production("test_nocond", "or_test | lambdef_nocond")
def convert_test_nocond(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    (child,) = children
    return child


@with_production("lambdef", "'lambda' [varargslist] ':' test")
@with_production("lambdef_nocond", "'lambda' [varargslist] ':' test_nocond")
def convert_lambda(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    lambdatoken, *params, colontoken, test = children

    # Grab the whitespace around the colon. If there are no params, then
    # the colon owns the whitespace before and after it. If there are
    # any params, then the last param owns the whitespace before the colon.
    # We handle the parameter movement below.
    colon = Colon(
        whitespace_before=parse_parenthesizable_whitespace(
            config, colontoken.whitespace_before
        ),
        whitespace_after=parse_parenthesizable_whitespace(
            config, colontoken.whitespace_after
        ),
    )

    # Unpack optional parameters
    if len(params) == 0:
        parameters = Parameters()
        whitespace_after_lambda = MaybeSentinel.DEFAULT
    else:
        (parameters,) = params
        whitespace_after_lambda = parse_parenthesizable_whitespace(
            config, lambdatoken.whitespace_after
        )

        # Handle pre-colon whitespace
        if parameters.star_kwarg is not None:
            if parameters.star_kwarg.comma == MaybeSentinel.DEFAULT:
                parameters = parameters.with_changes(
                    star_kwarg=parameters.star_kwarg.with_changes(
                        whitespace_after_param=colon.whitespace_before
                    )
                )
        elif parameters.kwonly_params:
            if parameters.kwonly_params[-1].comma == MaybeSentinel.DEFAULT:
                parameters = parameters.with_changes(
                    kwonly_params=(
                        *parameters.kwonly_params[:-1],
                        parameters.kwonly_params[-1].with_changes(
                            whitespace_after_param=colon.whitespace_before
                        ),
                    )
                )
        elif isinstance(parameters.star_arg, Param):
            if parameters.star_arg.comma == MaybeSentinel.DEFAULT:
                parameters = parameters.with_changes(
                    star_arg=parameters.star_arg.with_changes(
                        whitespace_after_param=colon.whitespace_before
                    )
                )
        elif parameters.params:
            if parameters.params[-1].comma == MaybeSentinel.DEFAULT:
                parameters = parameters.with_changes(
                    params=(
                        *parameters.params[:-1],
                        parameters.params[-1].with_changes(
                            whitespace_after_param=colon.whitespace_before
                        ),
                    )
                )

        # Colon doesn't own its own pre-whitespace now.
        colon = colon.with_changes(whitespace_before=SimpleWhitespace(""))

    # Return a lambda
    return WithLeadingWhitespace(
        Lambda(
            whitespace_after_lambda=whitespace_after_lambda,
            params=parameters,
            body=test.value,
            colon=colon,
        ),
        lambdatoken.whitespace_before,
    )


@with_production("or_test", "and_test ('or' and_test)*")
@with_production("and_test", "not_test ('and' not_test)*")
def convert_boolop(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    leftexpr, *rightexprs = children
    if len(rightexprs) == 0:
        return leftexpr

    whitespace_before = leftexpr.whitespace_before
    leftexpr = leftexpr.value

    # Convert all of the operations that have no precedence in a loop
    for op, rightexpr in grouper(rightexprs, 2):
        if op.string not in BOOLOP_TOKEN_LUT:
            raise ParserSyntaxError(
                f"Unexpected token '{op.string}'!",
                lines=config.lines,
                raw_line=0,
                raw_column=0,
            )
        leftexpr = BooleanOperation(
            left=leftexpr,
            # pyre-ignore Pyre thinks that the type of the LUT is CSTNode.
            operator=BOOLOP_TOKEN_LUT[op.string](
                whitespace_before=parse_parenthesizable_whitespace(
                    config, op.whitespace_before
                ),
                whitespace_after=parse_parenthesizable_whitespace(
                    config, op.whitespace_after
                ),
            ),
            right=rightexpr.value,
        )
    return WithLeadingWhitespace(leftexpr, whitespace_before)


@with_production("not_test", "'not' not_test | comparison")
def convert_not_test(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    if len(children) == 1:
        (child,) = children
        return child
    else:
        nottoken, nottest = children
        return WithLeadingWhitespace(
            UnaryOperation(
                operator=Not(
                    whitespace_after=parse_parenthesizable_whitespace(
                        config, nottoken.whitespace_after
                    )
                ),
                expression=nottest.value,
            ),
            nottoken.whitespace_before,
        )


@with_production("comparison", "expr (comp_op expr)*")
def convert_comparison(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    if len(children) == 1:
        (child,) = children
        return child

    lhs, *rest = children

    comparisons: typing.List[ComparisonTarget] = []
    for operator, comparator in grouper(rest, 2):
        comparisons.append(
            ComparisonTarget(operator=operator, comparator=comparator.value)
        )

    return WithLeadingWhitespace(
        Comparison(left=lhs.value, comparisons=tuple(comparisons)),
        lhs.whitespace_before,
    )


@with_production(
    "comp_op", "('<'|'>'|'=='|'>='|'<='|'<>'|'!='|'in'|'not' 'in'|'is'|'is' 'not')"
)
def convert_comp_op(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    if len(children) == 1:
        (op,) = children
        if op.string in COMPOP_TOKEN_LUT:
            # A regular comparison containing one token
            # pyre-ignore Pyre thinks that the type of the LUT is CSTNode.
            return COMPOP_TOKEN_LUT[op.string](
                whitespace_before=parse_parenthesizable_whitespace(
                    config, op.whitespace_before
                ),
                whitespace_after=parse_parenthesizable_whitespace(
                    config, op.whitespace_after
                ),
            )
        elif op.string in ["!=", "<>"]:
            # Not equal, which can take two forms in some cases
            return NotEqual(
                whitespace_before=parse_parenthesizable_whitespace(
                    config, op.whitespace_before
                ),
                value=op.string,
                whitespace_after=parse_parenthesizable_whitespace(
                    config, op.whitespace_after
                ),
            )
        else:
            # this should be unreachable
            raise ParserSyntaxError(
                f"Unexpected token '{op.string}'!",
                lines=config.lines,
                raw_line=0,
                raw_column=0,
            )
    else:
        # A two-token comparison
        leftcomp, rightcomp = children

        if leftcomp.string == "not" and rightcomp.string == "in":
            return NotIn(
                whitespace_before=parse_parenthesizable_whitespace(
                    config, leftcomp.whitespace_before
                ),
                whitespace_between=parse_parenthesizable_whitespace(
                    config, leftcomp.whitespace_after
                ),
                whitespace_after=parse_parenthesizable_whitespace(
                    config, rightcomp.whitespace_after
                ),
            )
        elif leftcomp.string == "is" and rightcomp.string == "not":
            return IsNot(
                whitespace_before=parse_parenthesizable_whitespace(
                    config, leftcomp.whitespace_before
                ),
                whitespace_between=parse_parenthesizable_whitespace(
                    config, leftcomp.whitespace_after
                ),
                whitespace_after=parse_parenthesizable_whitespace(
                    config, rightcomp.whitespace_after
                ),
            )
        else:
            # this should be unreachable
            raise ParserSyntaxError(
                f"Unexpected token '{leftcomp.string} {rightcomp.string}'!",
                lines=config.lines,
                raw_line=0,
                raw_column=0,
            )


@with_production("star_expr", "'*' expr")
def convert_star_expr(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    star, expr = children
    return WithLeadingWhitespace(
        StarredElement(
            expr.value,
            whitespace_before_value=parse_parenthesizable_whitespace(
                config, expr.whitespace_before
            ),
            # atom is responsible for parenthesis and trailing_whitespace if they exist
            # testlist_comp, exprlist, dictorsetmaker, etc are responsible for the comma
            # if it exists.
        ),
        whitespace_before=star.whitespace_before,
    )


@with_production("expr", "xor_expr ('|' xor_expr)*")
@with_production("xor_expr", "and_expr ('^' and_expr)*")
@with_production("and_expr", "shift_expr ('&' shift_expr)*")
@with_production("shift_expr", "arith_expr (('<<'|'>>') arith_expr)*")
@with_production("arith_expr", "term (('+'|'-') term)*")
@with_production("term", "factor (('*'|'@'|'/'|'%'|'//') factor)*", version=">=3.5")
@with_production("term", "factor (('*'|'/'|'%'|'//') factor)*", version="<3.5")
def convert_binop(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    leftexpr, *rightexprs = children
    if len(rightexprs) == 0:
        return leftexpr

    whitespace_before = leftexpr.whitespace_before
    leftexpr = leftexpr.value

    # Convert all of the operations that have no precedence in a loop
    for op, rightexpr in grouper(rightexprs, 2):
        if op.string not in BINOP_TOKEN_LUT:
            raise ParserSyntaxError(
                f"Unexpected token '{op.string}'!",
                lines=config.lines,
                raw_line=0,
                raw_column=0,
            )
        leftexpr = BinaryOperation(
            left=leftexpr,
            # pyre-ignore Pyre thinks that the type of the LUT is CSTNode.
            operator=BINOP_TOKEN_LUT[op.string](
                whitespace_before=parse_parenthesizable_whitespace(
                    config, op.whitespace_before
                ),
                whitespace_after=parse_parenthesizable_whitespace(
                    config, op.whitespace_after
                ),
            ),
            right=rightexpr.value,
        )
    return WithLeadingWhitespace(leftexpr, whitespace_before)


@with_production("factor", "('+'|'-'|'~') factor | power")
def convert_factor(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    if len(children) == 1:
        (child,) = children
        return child

    op, factor = children

    # First, tokenize the unary operator
    if op.string == "+":
        opnode = Plus(
            whitespace_after=parse_parenthesizable_whitespace(
                config, op.whitespace_after
            )
        )
    elif op.string == "-":
        opnode = Minus(
            whitespace_after=parse_parenthesizable_whitespace(
                config, op.whitespace_after
            )
        )
    elif op.string == "~":
        opnode = BitInvert(
            whitespace_after=parse_parenthesizable_whitespace(
                config, op.whitespace_after
            )
        )
    else:
        raise ParserSyntaxError(
            f"Unexpected token '{op.string}'!",
            lines=config.lines,
            raw_line=0,
            raw_column=0,
        )

    return WithLeadingWhitespace(
        UnaryOperation(operator=opnode, expression=factor.value), op.whitespace_before
    )


@with_production("power", "atom_expr ['**' factor]")
def convert_power(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    if len(children) == 1:
        (child,) = children
        return child

    left, power, right = children
    return WithLeadingWhitespace(
        BinaryOperation(
            left=left.value,
            operator=Power(
                whitespace_before=parse_parenthesizable_whitespace(
                    config, power.whitespace_before
                ),
                whitespace_after=parse_parenthesizable_whitespace(
                    config, power.whitespace_after
                ),
            ),
            right=right.value,
        ),
        left.whitespace_before,
    )


@with_production("atom_expr", "atom_expr_await | atom_expr_trailer")
def convert_atom_expr(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    (child,) = children
    return child


@with_production("atom_expr_await", "AWAIT atom_expr_trailer")
def convert_atom_expr_await(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    keyword, expr = children
    return WithLeadingWhitespace(
        Await(
            whitespace_after_await=parse_parenthesizable_whitespace(
                config, keyword.whitespace_after
            ),
            expression=expr.value,
        ),
        keyword.whitespace_before,
    )


@with_production("atom_expr_trailer", "atom trailer*")
def convert_atom_expr_trailer(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    atom, *trailers = children
    whitespace_before = atom.whitespace_before
    atom = atom.value

    # Need to walk through all trailers from left to right and construct
    # a series of nodes based on each partial type. We can't do this with
    # left recursion due to limits in the parser.
    for trailer in trailers:
        if isinstance(trailer, SubscriptPartial):
            atom = Subscript(
                value=atom,
                whitespace_after_value=parse_parenthesizable_whitespace(
                    config, trailer.whitespace_before
                ),
                lbracket=trailer.lbracket,
                # pyre-fixme[6]: Expected `Sequence[SubscriptElement]` for 4th param
                #  but got `Union[typing.Sequence[SubscriptElement], Index, Slice]`.
                slice=trailer.slice,
                rbracket=trailer.rbracket,
            )
        elif isinstance(trailer, AttributePartial):
            atom = Attribute(value=atom, dot=trailer.dot, attr=trailer.attr)
        elif isinstance(trailer, CallPartial):
            # If the trailing argument doesn't have a comma, then it owns the
            # trailing whitespace before the rpar. Otherwise, the comma owns
            # it.
            if (
                len(trailer.args) > 0
                and trailer.args[-1].comma == MaybeSentinel.DEFAULT
            ):
                args = (
                    *trailer.args[:-1],
                    trailer.args[-1].with_changes(
                        whitespace_after_arg=trailer.rpar.whitespace_before
                    ),
                )
            else:
                args = trailer.args
            atom = Call(
                func=atom,
                whitespace_after_func=parse_parenthesizable_whitespace(
                    config, trailer.lpar.whitespace_before
                ),
                whitespace_before_args=trailer.lpar.value.whitespace_after,
                # pyre-fixme[6]: Expected `Sequence[Arg]` for 4th param but got
                #  `Tuple[object, ...]`.
                args=tuple(args),
            )
        else:
            # This is an invalid trailer, so lets give up
            raise CSTLogicError()
    return WithLeadingWhitespace(atom, whitespace_before)


@with_production(
    "trailer", "trailer_arglist | trailer_subscriptlist | trailer_attribute"
)
def convert_trailer(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    (child,) = children
    return child


@with_production("trailer_arglist", "'(' [arglist] ')'")
def convert_trailer_arglist(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    lpar, *arglist, rpar = children
    return CallPartial(
        lpar=WithLeadingWhitespace(
            LeftParen(
                whitespace_after=parse_parenthesizable_whitespace(
                    config, lpar.whitespace_after
                )
            ),
            lpar.whitespace_before,
        ),
        args=() if not arglist else arglist[0].args,
        rpar=RightParen(
            whitespace_before=parse_parenthesizable_whitespace(
                config, rpar.whitespace_before
            )
        ),
    )


@with_production("trailer_subscriptlist", "'[' subscriptlist ']'")
def convert_trailer_subscriptlist(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    (lbracket, subscriptlist, rbracket) = children
    return SubscriptPartial(
        lbracket=LeftSquareBracket(
            whitespace_after=parse_parenthesizable_whitespace(
                config, lbracket.whitespace_after
            )
        ),
        slice=subscriptlist.value,
        rbracket=RightSquareBracket(
            whitespace_before=parse_parenthesizable_whitespace(
                config, rbracket.whitespace_before
            )
        ),
        whitespace_before=lbracket.whitespace_before,
    )


@with_production("subscriptlist", "subscript (',' subscript)* [',']")
def convert_subscriptlist(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    # This is a list of SubscriptElement, so construct as such by grouping every
    # subscript with an optional comma and adding to a list.
    elements = []
    for slice, comma in grouper(children, 2):
        if comma is None:
            elements.append(SubscriptElement(slice=slice.value))
        else:
            elements.append(
                SubscriptElement(
                    slice=slice.value,
                    comma=Comma(
                        whitespace_before=parse_parenthesizable_whitespace(
                            config, comma.whitespace_before
                        ),
                        whitespace_after=parse_parenthesizable_whitespace(
                            config, comma.whitespace_after
                        ),
                    ),
                )
            )
    return WithLeadingWhitespace(elements, children[0].whitespace_before)


@with_production("subscript", "test | [test] ':' [test] [sliceop]")
def convert_subscript(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    if len(children) == 1 and not isinstance(children[0], Token):
        # This is just an index node
        (test,) = children
        return WithLeadingWhitespace(Index(test.value), test.whitespace_before)

    if isinstance(children[-1], SlicePartial):
        # We got a partial slice as the final param. Extract the final
        # bits of the full subscript.
        *others, sliceop = children
        whitespace_before = others[0].whitespace_before
        second_colon = sliceop.second_colon
        step = sliceop.step
    else:
        # We can just parse this below, without taking extras from the
        # partial child.
        others = children
        whitespace_before = others[0].whitespace_before
        second_colon = MaybeSentinel.DEFAULT
        step = None

    # We need to create a partial slice to pass up. So, align so we have
    # a list that's always [Optional[Test], Colon, Optional[Test]].
    if isinstance(others[0], Token):
        # First token is a colon, so insert an empty test on the LHS. We
        # know the RHS is a test since it's not a sliceop.
        slicechildren = [None, *others]
    else:
        # First token is non-colon, so its a test.
        slicechildren = [*others]

    if len(slicechildren) < 3:
        # Now, we have to fill in the RHS. We know its two long
        # at this point if its not already 3.
        slicechildren = [*slicechildren, None]

    lower, first_colon, upper = slicechildren
    return WithLeadingWhitespace(
        Slice(
            lower=lower.value if lower is not None else None,
            first_colon=Colon(
                whitespace_before=parse_parenthesizable_whitespace(
                    config,
                    first_colon.whitespace_before,
                ),
                whitespace_after=parse_parenthesizable_whitespace(
                    config,
                    first_colon.whitespace_after,
                ),
            ),
            upper=upper.value if upper is not None else None,
            second_colon=second_colon,
            step=step,
        ),
        whitespace_before=whitespace_before,
    )


@with_production("sliceop", "':' [test]")
def convert_sliceop(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    if len(children) == 2:
        colon, test = children
        step = test.value
    else:
        (colon,) = children
        step = None
    return SlicePartial(
        second_colon=Colon(
            whitespace_before=parse_parenthesizable_whitespace(
                config, colon.whitespace_before
            ),
            whitespace_after=parse_parenthesizable_whitespace(
                config, colon.whitespace_after
            ),
        ),
        step=step,
    )


@with_production("trailer_attribute", "'.' NAME")
def convert_trailer_attribute(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    dot, name = children
    return AttributePartial(
        dot=Dot(
            whitespace_before=parse_parenthesizable_whitespace(
                config, dot.whitespace_before
            ),
            whitespace_after=parse_parenthesizable_whitespace(
                config, dot.whitespace_after
            ),
        ),
        attr=Name(name.string),
    )


@with_production(
    "atom",
    "atom_parens | atom_squarebrackets | atom_curlybraces | atom_string | atom_basic | atom_ellipses",
)
def convert_atom(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    (child,) = children
    return child


@with_production("atom_basic", "NAME | NUMBER | 'None' | 'True' | 'False'")
def convert_atom_basic(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    (child,) = children
    if child.type.name == "NAME":
        # This also handles 'None', 'True', and 'False' directly, but we
        # keep it in the grammar to be more correct.
        return WithLeadingWhitespace(Name(child.string), child.whitespace_before)
    elif child.type.name == "NUMBER":
        # We must determine what type of number it is since we split node
        # types up this way.
        if re.fullmatch(INTNUMBER_RE, child.string):
            return WithLeadingWhitespace(Integer(child.string), child.whitespace_before)
        elif re.fullmatch(FLOATNUMBER_RE, child.string):
            return WithLeadingWhitespace(Float(child.string), child.whitespace_before)
        elif re.fullmatch(IMAGNUMBER_RE, child.string):
            return WithLeadingWhitespace(
                Imaginary(child.string), child.whitespace_before
            )
        else:
            raise ParserSyntaxError(
                f"Unparseable number {child.string}",
                lines=config.lines,
                raw_line=0,
                raw_column=0,
            )
    else:
        raise ParserSyntaxError(
            f"Logic error, unexpected token {child.type.name}",
            lines=config.lines,
            raw_line=0,
            raw_column=0,
        )


@with_production("atom_squarebrackets", "'[' [testlist_comp_list] ']'")
def convert_atom_squarebrackets(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    lbracket_tok, *body, rbracket_tok = children
    lbracket = LeftSquareBracket(
        whitespace_after=parse_parenthesizable_whitespace(
            config, lbracket_tok.whitespace_after
        )
    )

    rbracket = RightSquareBracket(
        whitespace_before=parse_parenthesizable_whitespace(
            config, rbracket_tok.whitespace_before
        )
    )

    if len(body) == 0:
        list_node = List((), lbracket=lbracket, rbracket=rbracket)
    else:  # len(body) == 1
        # body[0] is a List or ListComp
        list_node = body[0].value.with_changes(lbracket=lbracket, rbracket=rbracket)

    return WithLeadingWhitespace(list_node, lbracket_tok.whitespace_before)


@with_production("atom_curlybraces", "'{' [dictorsetmaker] '}'")
def convert_atom_curlybraces(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    lbrace_tok, *body, rbrace_tok = children
    lbrace = LeftCurlyBrace(
        whitespace_after=parse_parenthesizable_whitespace(
            config, lbrace_tok.whitespace_after
        )
    )

    rbrace = RightCurlyBrace(
        whitespace_before=parse_parenthesizable_whitespace(
            config, rbrace_tok.whitespace_before
        )
    )

    if len(body) == 0:
        dict_or_set_node = Dict((), lbrace=lbrace, rbrace=rbrace)
    else:  # len(body) == 1
        dict_or_set_node = body[0].value.with_changes(lbrace=lbrace, rbrace=rbrace)

    return WithLeadingWhitespace(dict_or_set_node, lbrace_tok.whitespace_before)


@with_production("atom_parens", "'(' [yield_expr|testlist_comp_tuple] ')'")
def convert_atom_parens(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    lpar_tok, *atoms, rpar_tok = children

    lpar = LeftParen(
        whitespace_after=parse_parenthesizable_whitespace(
            config, lpar_tok.whitespace_after
        )
    )

    rpar = RightParen(
        whitespace_before=parse_parenthesizable_whitespace(
            config, rpar_tok.whitespace_before
        )
    )

    if len(atoms) == 1:
        # inner_atom is a _BaseParenthesizedNode
        inner_atom = atoms[0].value
        return WithLeadingWhitespace(
            inner_atom.with_changes(
                # pyre-fixme[60]: Expected to unpack an iterable, but got `unknown`.
                lpar=(lpar, *inner_atom.lpar),
                # pyre-fixme[60]: Expected to unpack an iterable, but got `unknown`.
                rpar=(*inner_atom.rpar, rpar),
            ),
            lpar_tok.whitespace_before,
        )
    else:
        return WithLeadingWhitespace(
            Tuple((), lpar=(lpar,), rpar=(rpar,)), lpar_tok.whitespace_before
        )


@with_production("atom_ellipses", "'...'")
def convert_atom_ellipses(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    (token,) = children
    return WithLeadingWhitespace(Ellipsis(), token.whitespace_before)


@with_production("atom_string", "(STRING | fstring) [atom_string]")
def convert_atom_string(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    if len(children) == 1:
        return children[0]
    else:
        left, right = children
        return WithLeadingWhitespace(
            ConcatenatedString(
                left=left.value,
                whitespace_between=parse_parenthesizable_whitespace(
                    config, right.whitespace_before
                ),
                right=right.value,
            ),
            left.whitespace_before,
        )


@with_production("fstring", "FSTRING_START fstring_content* FSTRING_END")
def convert_fstring(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    start, *content, end = children
    return WithLeadingWhitespace(
        FormattedString(start=start.string, parts=tuple(content), end=end.string),
        start.whitespace_before,
    )


@with_production("fstring_content", "FSTRING_STRING | fstring_expr")
def convert_fstring_content(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    (child,) = children
    if isinstance(child, Token):
        # Construct and return a raw string portion.
        return FormattedStringText(child.string)
    else:
        # Pass the expression up one production.
        return child


@with_production("fstring_conversion", "'!' NAME")
def convert_fstring_conversion(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    exclaim, name = children
    # There cannot be a space between the two tokens, so no need to preserve this.
    return FormattedStringConversionPartial(name.string, exclaim.whitespace_before)


@with_production("fstring_equality", "'='", version=">=3.8")
def convert_fstring_equality(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    (equal,) = children
    return AssignEqual(
        whitespace_before=parse_parenthesizable_whitespace(
            config, equal.whitespace_before
        ),
        whitespace_after=parse_parenthesizable_whitespace(
            config, equal.whitespace_after
        ),
    )


@with_production(
    "fstring_expr",
    "'{' (testlist_comp_tuple | yield_expr) [ fstring_equality ] [ fstring_conversion ] [ fstring_format_spec ] '}'",
    version=">=3.8",
)
@with_production(
    "fstring_expr",
    "'{' (testlist_comp_tuple | yield_expr) [ fstring_conversion ] [ fstring_format_spec ] '}'",
    version="<3.8",
)
def convert_fstring_expr(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    openbrkt, testlist, *conversions, closebrkt = children

    # Extract any optional equality (self-debugging expressions)
    if len(conversions) > 0 and isinstance(conversions[0], AssignEqual):
        equal = conversions[0]
        conversions = conversions[1:]
    else:
        equal = None

    # Extract any optional conversion
    if len(conversions) > 0 and isinstance(
        conversions[0], FormattedStringConversionPartial
    ):
        conversion = conversions[0].value
        conversions = conversions[1:]
    else:
        conversion = None

    # Extract any optional format spec
    if len(conversions) > 0:
        format_spec = conversions[0].values
    else:
        format_spec = None

    # Fix up any spacing issue we find due to the fact that the equal can
    # have whitespace and is also at the end of the expression.
    if equal is not None:
        whitespace_after_expression = SimpleWhitespace("")
    else:
        whitespace_after_expression = parse_parenthesizable_whitespace(
            config, children[2].whitespace_before
        )

    return FormattedStringExpression(
        whitespace_before_expression=parse_parenthesizable_whitespace(
            config, testlist.whitespace_before
        ),
        expression=testlist.value,
        equal=equal,
        whitespace_after_expression=whitespace_after_expression,
        conversion=conversion,
        format_spec=format_spec,
    )


@with_production("fstring_format_spec", "':' fstring_content*")
def convert_fstring_format_spec(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    colon, *content = children
    return FormattedStringFormatSpecPartial(tuple(content), colon.whitespace_before)


@with_production(
    "testlist_comp_tuple",
    "(namedexpr_test|star_expr) ( comp_for | (',' (namedexpr_test|star_expr))* [','] )",
    version=">=3.8",
)
@with_production(
    "testlist_comp_tuple",
    "(test|star_expr) ( comp_for | (',' (test|star_expr))* [','] )",
    version=">=3.5,<3.8",
)
@with_production(
    "testlist_comp_tuple",
    "(test) ( comp_for | (',' (test))* [','] )",
    version="<3.5",
)
def convert_testlist_comp_tuple(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    return _convert_testlist_comp(
        config,
        children,
        single_child_is_sequence=False,
        sequence_type=Tuple,
        comprehension_type=GeneratorExp,
    )


@with_production(
    "testlist_comp_list",
    "(namedexpr_test|star_expr) ( comp_for | (',' (namedexpr_test|star_expr))* [','] )",
    version=">=3.8",
)
@with_production(
    "testlist_comp_list",
    "(test|star_expr) ( comp_for | (',' (test|star_expr))* [','] )",
    version=">=3.5,<3.8",
)
@with_production(
    "testlist_comp_list",
    "(test) ( comp_for | (',' (test))* [','] )",
    version="<3.5",
)
def convert_testlist_comp_list(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    return _convert_testlist_comp(
        config,
        children,
        single_child_is_sequence=True,
        sequence_type=List,
        comprehension_type=ListComp,
    )


def _convert_testlist_comp(
    config: ParserConfig,
    children: typing.Sequence[typing.Any],
    single_child_is_sequence: bool,
    sequence_type: typing.Union[
        typing.Type[Tuple], typing.Type[List], typing.Type[Set]
    ],
    comprehension_type: typing.Union[
        typing.Type[GeneratorExp], typing.Type[ListComp], typing.Type[SetComp]
    ],
) -> typing.Any:
    # This is either a single-element list, or the second token is a comma, so we're not
    # in a generator.
    if len(children) == 1 or isinstance(children[1], Token):
        return _convert_sequencelike(
            config, children, single_child_is_sequence, sequence_type
        )
    else:
        # N.B. The parent node (e.g. atom) is responsible for computing and attaching
        # whitespace information on any parenthesis, square brackets, or curly braces
        elt, for_in = children
        return WithLeadingWhitespace(
            comprehension_type(elt=elt.value, for_in=for_in, lpar=(), rpar=()),
            elt.whitespace_before,
        )


@with_production("testlist_star_expr", "(test|star_expr) (',' (test|star_expr))* [',']")
@with_production("testlist", "test (',' test)* [',']")
@with_production("exprlist", "(expr|star_expr) (',' (expr|star_expr))* [',']")
def convert_test_or_expr_list(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    # Used by expression statements and assignments. Neither of these cases want to
    # treat a single child as a sequence.
    return _convert_sequencelike(
        config, children, single_child_is_sequence=False, sequence_type=Tuple
    )


def _convert_sequencelike(
    config: ParserConfig,
    children: typing.Sequence[typing.Any],
    single_child_is_sequence: bool,
    sequence_type: typing.Union[
        typing.Type[Tuple], typing.Type[List], typing.Type[Set]
    ],
) -> typing.Any:
    if not single_child_is_sequence and len(children) == 1:
        return children[0]
    # N.B. The parent node (e.g. atom) is responsible for computing and attaching
    # whitespace information on any parenthesis, square brackets, or curly braces
    elements = []
    for wrapped_expr_or_starred_element, comma_token in grouper(children, 2):
        expr_or_starred_element = wrapped_expr_or_starred_element.value
        if comma_token is None:
            comma = MaybeSentinel.DEFAULT
        else:
            comma = Comma(
                whitespace_before=parse_parenthesizable_whitespace(
                    config, comma_token.whitespace_before
                ),
                # Only compute whitespace_after if we're not a trailing comma.
                # If we're a trailing comma, that whitespace should be consumed by the
                # TrailingWhitespace, parenthesis, etc.
                whitespace_after=(
                    parse_parenthesizable_whitespace(
                        config, comma_token.whitespace_after
                    )
                    if comma_token is not children[-1]
                    else SimpleWhitespace("")
                ),
            )

        if isinstance(expr_or_starred_element, StarredElement):
            starred_element = expr_or_starred_element
            elements.append(starred_element.with_changes(comma=comma))
        else:
            expr = expr_or_starred_element
            elements.append(Element(value=expr, comma=comma))

    # lpar/rpar are the responsibility of our parent
    return WithLeadingWhitespace(
        sequence_type(elements, lpar=(), rpar=()),
        children[0].whitespace_before,
    )


@with_production(
    "dictorsetmaker",
    (
        "( ((test ':' test | '**' expr)"
        + " (comp_for | (',' (test ':' test | '**' expr))* [','])) |"
        + "((test | star_expr) "
        + " (comp_for | (',' (test | star_expr))* [','])) )"
    ),
    version=">=3.5",
)
@with_production(
    "dictorsetmaker",
    (
        "( ((test ':' test)"
        + " (comp_for | (',' (test ':' test))* [','])) |"
        + "((test) "
        + " (comp_for | (',' (test))* [','])) )"
    ),
    version="<3.5",
)
def convert_dictorsetmaker(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    # We'll always have at least one child. `atom_curlybraces` handles empty
    # dicts.
    if len(children) > 1 and (
        (isinstance(children[1], Token) and children[1].string == ":")
        or (isinstance(children[0], Token) and children[0].string == "**")
    ):
        return _convert_dict(config, children)
    else:
        return _convert_set(config, children)


def _convert_dict_element(
    config: ParserConfig,
    children_iter: typing.Iterator[typing.Any],
    last_child: typing.Any,
) -> typing.Union[DictElement, StarredDictElement]:
    first = next(children_iter)
    if isinstance(first, Token) and first.string == "**":
        expr = next(children_iter)
        element = StarredDictElement(
            expr.value,
            whitespace_before_value=parse_parenthesizable_whitespace(
                config, expr.whitespace_before
            ),
        )
    else:
        key = first
        colon_tok = next(children_iter)
        value = next(children_iter)
        element = DictElement(
            key.value,
            value.value,
            whitespace_before_colon=parse_parenthesizable_whitespace(
                config, colon_tok.whitespace_before
            ),
            whitespace_after_colon=parse_parenthesizable_whitespace(
                config, colon_tok.whitespace_after
            ),
        )
    # Handle the trailing comma (if there is one)
    try:
        comma_token = next(children_iter)
        element = element.with_changes(
            comma=Comma(
                whitespace_before=parse_parenthesizable_whitespace(
                    config, comma_token.whitespace_before
                ),
                # Only compute whitespace_after if we're not a trailing comma.
                # If we're a trailing comma, that whitespace should be consumed by the
                # RightBracket.
                whitespace_after=(
                    parse_parenthesizable_whitespace(
                        config, comma_token.whitespace_after
                    )
                    if comma_token is not last_child
                    else SimpleWhitespace("")
                ),
            )
        )
    except StopIteration:
        pass
    return element


def _convert_dict(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    is_first_starred = isinstance(children[0], Token) and children[0].string == "**"
    if is_first_starred:
        possible_comp_for = None if len(children) < 3 else children[2]
    else:
        possible_comp_for = None if len(children) < 4 else children[3]
    if isinstance(possible_comp_for, CompFor):
        if is_first_starred:
            raise PartialParserSyntaxError(
                "dict unpacking cannot be used in dict comprehension"
            )
        return _convert_dict_comp(config, children)

    children_iter = iter(children)
    last_child = children[-1]
    elements = []
    while True:
        try:
            elements.append(_convert_dict_element(config, children_iter, last_child))
        except StopIteration:
            break
    # lbrace, rbrace, lpar, and rpar will be attached as-needed by the atom grammar
    return WithLeadingWhitespace(Dict(tuple(elements)), children[0].whitespace_before)


def _convert_dict_comp(config, children: typing.Sequence[typing.Any]) -> typing.Any:
    key, colon_token, value, comp_for = children
    return WithLeadingWhitespace(
        DictComp(
            key.value,
            value.value,
            comp_for,
            # lbrace, rbrace, lpar, and rpar will be attached as-needed by the atom grammar
            whitespace_before_colon=parse_parenthesizable_whitespace(
                config, colon_token.whitespace_before
            ),
            whitespace_after_colon=parse_parenthesizable_whitespace(
                config, colon_token.whitespace_after
            ),
        ),
        key.whitespace_before,
    )


def _convert_set(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    return _convert_testlist_comp(
        config,
        children,
        single_child_is_sequence=True,
        sequence_type=Set,
        comprehension_type=SetComp,
    )


@with_production("arglist", "argument (',' argument)* [',']")
def convert_arglist(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    args = []
    for argument, comma in grouper(children, 2):
        if comma is None:
            args.append(argument)
        else:
            args.append(
                argument.with_changes(
                    comma=Comma(
                        whitespace_before=parse_parenthesizable_whitespace(
                            config, comma.whitespace_before
                        ),
                        whitespace_after=parse_parenthesizable_whitespace(
                            config, comma.whitespace_after
                        ),
                    )
                )
            )
    return ArglistPartial(args)


@with_production("argument", "arg_assign_comp_for | star_arg")
def convert_argument(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    (child,) = children
    return child


@with_production(
    "arg_assign_comp_for", "test [comp_for] | test '=' test", version="<=3.7"
)
@with_production(
    "arg_assign_comp_for",
    "test [comp_for] | test ':=' test | test '=' test",
    version=">=3.8",
)
def convert_arg_assign_comp_for(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    if len(children) == 1:
        # Simple test
        (child,) = children
        return Arg(value=child.value)
    elif len(children) == 2:
        elt, for_in = children
        return Arg(value=GeneratorExp(elt.value, for_in, lpar=(), rpar=()))
    else:
        lhs, equal, rhs = children
        # "key := value" assignment; positional
        if equal.string == ":=":
            val = convert_namedexpr_test(config, children)
            if not isinstance(val, WithLeadingWhitespace):
                raise TypeError(
                    f"convert_namedexpr_test returned {val!r}, not WithLeadingWhitespace"
                )
            return Arg(value=val.value)
        # "key = value" assignment; keyword argument
        return Arg(
            keyword=lhs.value,
            equal=AssignEqual(
                whitespace_before=parse_parenthesizable_whitespace(
                    config, equal.whitespace_before
                ),
                whitespace_after=parse_parenthesizable_whitespace(
                    config, equal.whitespace_after
                ),
            ),
            value=rhs.value,
        )


@with_production("star_arg", "'**' test | '*' test")
def convert_star_arg(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    star, test = children
    return Arg(
        star=star.string,
        whitespace_after_star=parse_parenthesizable_whitespace(
            config, star.whitespace_after
        ),
        value=test.value,
    )


@with_production("sync_comp_for", "'for' exprlist 'in' or_test comp_if* [comp_for]")
def convert_sync_comp_for(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    # unpack
    for_tok, target, in_tok, iter, *trailing = children
    if len(trailing) and isinstance(trailing[-1], CompFor):
        *ifs, inner_for_in = trailing
    else:
        ifs, inner_for_in = trailing, None

    return CompFor(
        target=target.value,
        iter=iter.value,
        ifs=ifs,
        inner_for_in=inner_for_in,
        whitespace_before=parse_parenthesizable_whitespace(
            config, for_tok.whitespace_before
        ),
        whitespace_after_for=parse_parenthesizable_whitespace(
            config, for_tok.whitespace_after
        ),
        whitespace_before_in=parse_parenthesizable_whitespace(
            config, in_tok.whitespace_before
        ),
        whitespace_after_in=parse_parenthesizable_whitespace(
            config, in_tok.whitespace_after
        ),
    )


@with_production("comp_for", "[ASYNC] sync_comp_for", version=">=3.6")
@with_production("comp_for", "sync_comp_for", version="<=3.5")
def convert_comp_for(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    if len(children) == 1:
        (sync_comp_for,) = children
        return sync_comp_for
    else:
        (async_tok, sync_comp_for) = children
        return sync_comp_for.with_changes(
            # asynchronous steals the `CompFor`'s `whitespace_before`.
            asynchronous=Asynchronous(whitespace_after=sync_comp_for.whitespace_before),
            # But, in exchange, `CompFor` gets to keep `async_tok`'s leading
            # whitespace, because that's now the beginning of the `CompFor`.
            whitespace_before=parse_parenthesizable_whitespace(
                config, async_tok.whitespace_before
            ),
        )


@with_production("comp_if", "'if' test_nocond")
def convert_comp_if(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    if_tok, test = children
    return CompIf(
        test.value,
        whitespace_before=parse_parenthesizable_whitespace(
            config, if_tok.whitespace_before
        ),
        whitespace_before_test=parse_parenthesizable_whitespace(
            config, test.whitespace_before
        ),
    )


@with_production("yield_expr", "'yield' [yield_arg]")
def convert_yield_expr(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    if len(children) == 1:
        # Yielding implicit none
        (yield_token,) = children
        yield_node = Yield(value=None)
    else:
        # Yielding explicit value
        (yield_token, yield_arg) = children
        yield_node = Yield(
            value=yield_arg.value,
            whitespace_after_yield=parse_parenthesizable_whitespace(
                config, yield_arg.whitespace_before
            ),
        )

    return WithLeadingWhitespace(yield_node, yield_token.whitespace_before)


@with_production("yield_arg", "testlist", version="<3.3")
@with_production("yield_arg", "'from' test | testlist", version=">=3.3,<3.8")
@with_production("yield_arg", "'from' test | testlist_star_expr", version=">=3.8")
def convert_yield_arg(
    config: ParserConfig, children: typing.Sequence[typing.Any]
) -> typing.Any:
    if len(children) == 1:
        # Just a regular testlist, pass it up
        (child,) = children
        return child
    else:
        # Its a yield from
        (from_token, test) = children

        return WithLeadingWhitespace(
            From(
                item=test.value,
                whitespace_after_from=parse_parenthesizable_whitespace(
                    config, test.whitespace_before
                ),
            ),
            from_token.whitespace_before,
        )
