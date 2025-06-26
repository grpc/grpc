# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
# pyre-unsafe

from typing import Any, Dict, List, Optional, Sequence, Tuple, Type

from libcst import CSTLogicError
from libcst._exceptions import ParserSyntaxError, PartialParserSyntaxError
from libcst._maybe_sentinel import MaybeSentinel
from libcst._nodes.expression import (
    Annotation,
    Arg,
    Asynchronous,
    Attribute,
    Call,
    From,
    LeftParen,
    Name,
    Param,
    Parameters,
    RightParen,
)
from libcst._nodes.op import (
    AddAssign,
    AssignEqual,
    BaseAugOp,
    BitAndAssign,
    BitOrAssign,
    BitXorAssign,
    Comma,
    DivideAssign,
    Dot,
    FloorDivideAssign,
    ImportStar,
    LeftShiftAssign,
    MatrixMultiplyAssign,
    ModuloAssign,
    MultiplyAssign,
    PowerAssign,
    RightShiftAssign,
    Semicolon,
    SubtractAssign,
)
from libcst._nodes.statement import (
    AnnAssign,
    AsName,
    Assert,
    Assign,
    AssignTarget,
    AugAssign,
    Break,
    ClassDef,
    Continue,
    Decorator,
    Del,
    Else,
    ExceptHandler,
    Expr,
    Finally,
    For,
    FunctionDef,
    Global,
    If,
    Import,
    ImportAlias,
    ImportFrom,
    IndentedBlock,
    NameItem,
    Nonlocal,
    Pass,
    Raise,
    Return,
    SimpleStatementLine,
    SimpleStatementSuite,
    Try,
    While,
    With,
    WithItem,
)
from libcst._nodes.whitespace import EmptyLine, SimpleWhitespace
from libcst._parser.custom_itertools import grouper
from libcst._parser.production_decorator import with_production
from libcst._parser.types.config import ParserConfig
from libcst._parser.types.partials import (
    AnnAssignPartial,
    AssignPartial,
    AugAssignPartial,
    DecoratorPartial,
    ExceptClausePartial,
    FuncdefPartial,
    ImportPartial,
    ImportRelativePartial,
    SimpleStatementPartial,
    WithLeadingWhitespace,
)
from libcst._parser.types.token import Token
from libcst._parser.whitespace_parser import (
    parse_empty_lines,
    parse_parenthesizable_whitespace,
    parse_simple_whitespace,
)

AUGOP_TOKEN_LUT: Dict[str, Type[BaseAugOp]] = {
    "+=": AddAssign,
    "-=": SubtractAssign,
    "*=": MultiplyAssign,
    "@=": MatrixMultiplyAssign,
    "/=": DivideAssign,
    "%=": ModuloAssign,
    "&=": BitAndAssign,
    "|=": BitOrAssign,
    "^=": BitXorAssign,
    "<<=": LeftShiftAssign,
    ">>=": RightShiftAssign,
    "**=": PowerAssign,
    "//=": FloorDivideAssign,
}


@with_production("stmt_input", "stmt ENDMARKER")
def convert_stmt_input(config: ParserConfig, children: Sequence[Any]) -> Any:
    (child, endmarker) = children
    return child


@with_production("stmt", "simple_stmt_line | compound_stmt")
def convert_stmt(config: ParserConfig, children: Sequence[Any]) -> Any:
    (child,) = children
    return child


@with_production("simple_stmt_partial", "small_stmt (';' small_stmt)* [';'] NEWLINE")
def convert_simple_stmt_partial(config: ParserConfig, children: Sequence[Any]) -> Any:
    *statements, trailing_whitespace = children

    last_stmt = len(statements) / 2
    body = []
    for i, (stmt_body, semi) in enumerate(grouper(statements, 2)):
        if semi is not None:
            if i == (last_stmt - 1):
                # Trailing semicolons only own the whitespace before.
                semi = Semicolon(
                    whitespace_before=parse_simple_whitespace(
                        config, semi.whitespace_before
                    ),
                    whitespace_after=SimpleWhitespace(""),
                )
            else:
                # Middle semicolons own the whitespace before and after.
                semi = Semicolon(
                    whitespace_before=parse_simple_whitespace(
                        config, semi.whitespace_before
                    ),
                    whitespace_after=parse_simple_whitespace(
                        config, semi.whitespace_after
                    ),
                )
        else:
            semi = MaybeSentinel.DEFAULT
        body.append(stmt_body.value.with_changes(semicolon=semi))
    return SimpleStatementPartial(
        body,
        whitespace_before=statements[0].whitespace_before,
        trailing_whitespace=trailing_whitespace,
    )


@with_production("simple_stmt_line", "simple_stmt_partial")
def convert_simple_stmt_line(config: ParserConfig, children: Sequence[Any]) -> Any:
    """
    This function is similar to convert_simple_stmt_suite, but yields a different type
    """
    (partial,) = children
    return SimpleStatementLine(
        partial.body,
        leading_lines=parse_empty_lines(config, partial.whitespace_before),
        trailing_whitespace=partial.trailing_whitespace,
    )


@with_production("simple_stmt_suite", "simple_stmt_partial")
def convert_simple_stmt_suite(config: ParserConfig, children: Sequence[Any]) -> Any:
    """
    This function is similar to convert_simple_stmt_line, but yields a different type
    """
    (partial,) = children
    return SimpleStatementSuite(
        partial.body,
        leading_whitespace=parse_simple_whitespace(config, partial.whitespace_before),
        trailing_whitespace=partial.trailing_whitespace,
    )


@with_production(
    "small_stmt",
    (
        "expr_stmt | del_stmt | pass_stmt | break_stmt | continue_stmt | return_stmt"
        + "| raise_stmt | yield_stmt | import_stmt | global_stmt | nonlocal_stmt"
        + "| assert_stmt"
    ),
)
def convert_small_stmt(config: ParserConfig, children: Sequence[Any]) -> Any:
    # Doesn't construct SmallStatement, because we don't know about semicolons yet.
    # convert_simple_stmt will construct the SmallStatement nodes.
    (small_stmt_body,) = children
    return small_stmt_body


@with_production(
    "expr_stmt",
    "testlist_star_expr (annassign | augassign | assign* )",
    version=">=3.6",
)
@with_production(
    "expr_stmt", "testlist_star_expr (augassign | assign* )", version="<=3.5"
)
@with_production("yield_stmt", "yield_expr")
def convert_expr_stmt(config: ParserConfig, children: Sequence[Any]) -> Any:
    if len(children) == 1:
        # This is an unassigned expr statement (like a function call)
        (test_node,) = children
        return WithLeadingWhitespace(
            Expr(value=test_node.value), test_node.whitespace_before
        )
    elif len(children) == 2:
        lhs, rhs = children
        if isinstance(rhs, AnnAssignPartial):
            return WithLeadingWhitespace(
                AnnAssign(
                    target=lhs.value,
                    annotation=rhs.annotation,
                    equal=MaybeSentinel.DEFAULT if rhs.equal is None else rhs.equal,
                    value=rhs.value,
                ),
                lhs.whitespace_before,
            )
        elif isinstance(rhs, AugAssignPartial):
            return WithLeadingWhitespace(
                AugAssign(target=lhs.value, operator=rhs.operator, value=rhs.value),
                lhs.whitespace_before,
            )
    # The only thing it could be at this point is an assign with one or more targets.
    # So, walk the children moving the equals ownership back one and constructing a
    # list of AssignTargets.
    targets = []
    for i in range(len(children) - 1):
        target = children[i].value
        equal = children[i + 1].equal

        targets.append(
            AssignTarget(
                target=target,
                whitespace_before_equal=equal.whitespace_before,
                whitespace_after_equal=equal.whitespace_after,
            )
        )

    return WithLeadingWhitespace(
        Assign(targets=tuple(targets), value=children[-1].value),
        children[0].whitespace_before,
    )


@with_production("annassign", "':' test ['=' test]", version=">=3.6,<3.8")
@with_production(
    "annassign", "':' test ['=' (yield_expr|testlist_star_expr)]", version=">=3.8"
)
def convert_annassign(config: ParserConfig, children: Sequence[Any]) -> Any:
    if len(children) == 2:
        # Variable annotation only
        colon, annotation = children
        annotation = annotation.value
        equal = None
        value = None
    elif len(children) == 4:
        # Variable annotation and assignment
        colon, annotation, equal, value = children
        annotation = annotation.value
        value = value.value
        equal = AssignEqual(
            whitespace_before=parse_simple_whitespace(config, equal.whitespace_before),
            whitespace_after=parse_simple_whitespace(config, equal.whitespace_after),
        )
    else:
        raise ParserSyntaxError(
            "Invalid parser state!", lines=config.lines, raw_line=0, raw_column=0
        )

    return AnnAssignPartial(
        annotation=Annotation(
            whitespace_before_indicator=parse_simple_whitespace(
                config, colon.whitespace_before
            ),
            whitespace_after_indicator=parse_simple_whitespace(
                config, colon.whitespace_after
            ),
            annotation=annotation,
        ),
        equal=equal,
        value=value,
    )


@with_production(
    "augassign",
    (
        "('+=' | '-=' | '*=' | '@=' | '/=' | '%=' | '&=' | '|=' | '^=' | '<<=' | "
        + "'>>=' | '**=' | '//=') (yield_expr | testlist)"
    ),
    version=">=3.5",
)
@with_production(
    "augassign",
    (
        "('+=' | '-=' | '*=' | '/=' | '%=' | '&=' | '|=' | '^=' | '<<=' | "
        + "'>>=' | '**=' | '//=') (yield_expr | testlist)"
    ),
    version="<3.5",
)
def convert_augassign(config: ParserConfig, children: Sequence[Any]) -> Any:
    op, expr = children
    if op.string not in AUGOP_TOKEN_LUT:
        raise ParserSyntaxError(
            f"Unexpected token '{op.string}'!",
            lines=config.lines,
            raw_line=0,
            raw_column=0,
        )

    return AugAssignPartial(
        # pyre-ignore Pyre seems to think that the value of this LUT is CSTNode
        operator=AUGOP_TOKEN_LUT[op.string](
            whitespace_before=parse_simple_whitespace(config, op.whitespace_before),
            whitespace_after=parse_simple_whitespace(config, op.whitespace_after),
        ),
        value=expr.value,
    )


@with_production("assign", "'=' (yield_expr|testlist_star_expr)")
def convert_assign(config: ParserConfig, children: Sequence[Any]) -> Any:
    equal, expr = children
    return AssignPartial(
        equal=AssignEqual(
            whitespace_before=parse_simple_whitespace(config, equal.whitespace_before),
            whitespace_after=parse_simple_whitespace(config, equal.whitespace_after),
        ),
        value=expr.value,
    )


@with_production("pass_stmt", "'pass'")
def convert_pass_stmt(config: ParserConfig, children: Sequence[Any]) -> Any:
    (name,) = children
    return WithLeadingWhitespace(Pass(), name.whitespace_before)


@with_production("del_stmt", "'del' exprlist")
def convert_del_stmt(config: ParserConfig, children: Sequence[Any]) -> Any:
    (del_name, exprlist) = children
    return WithLeadingWhitespace(
        Del(
            target=exprlist.value,
            whitespace_after_del=parse_simple_whitespace(
                config, del_name.whitespace_after
            ),
        ),
        del_name.whitespace_before,
    )


@with_production("continue_stmt", "'continue'")
def convert_continue_stmt(config: ParserConfig, children: Sequence[Any]) -> Any:
    (name,) = children
    return WithLeadingWhitespace(Continue(), name.whitespace_before)


@with_production("break_stmt", "'break'")
def convert_break_stmt(config: ParserConfig, children: Sequence[Any]) -> Any:
    (name,) = children
    return WithLeadingWhitespace(Break(), name.whitespace_before)


@with_production("return_stmt", "'return' [testlist]", version="<=3.7")
@with_production("return_stmt", "'return' [testlist_star_expr]", version=">=3.8")
def convert_return_stmt(config: ParserConfig, children: Sequence[Any]) -> Any:
    if len(children) == 1:
        (keyword,) = children
        return WithLeadingWhitespace(
            Return(whitespace_after_return=SimpleWhitespace("")),
            keyword.whitespace_before,
        )
    else:
        (keyword, testlist) = children
        return WithLeadingWhitespace(
            Return(
                value=testlist.value,
                whitespace_after_return=parse_simple_whitespace(
                    config, keyword.whitespace_after
                ),
            ),
            keyword.whitespace_before,
        )


@with_production("import_stmt", "import_name | import_from")
def convert_import_stmt(config: ParserConfig, children: Sequence[Any]) -> Any:
    (child,) = children
    return child


@with_production("import_name", "'import' dotted_as_names")
def convert_import_name(config: ParserConfig, children: Sequence[Any]) -> Any:
    importtoken, names = children
    return WithLeadingWhitespace(
        Import(
            names=names.names,
            whitespace_after_import=parse_simple_whitespace(
                config, importtoken.whitespace_after
            ),
        ),
        importtoken.whitespace_before,
    )


@with_production("import_relative", "('.' | '...')* dotted_name | ('.' | '...')+")
def convert_import_relative(config: ParserConfig, children: Sequence[Any]) -> Any:
    dots = []
    dotted_name = None
    for child in children:
        if isinstance(child, Token):
            # Special case for "...", which is part of the grammar
            if child.string == "...":
                dots.extend(
                    [
                        Dot(),
                        Dot(),
                        Dot(
                            whitespace_after=parse_simple_whitespace(
                                config, child.whitespace_after
                            )
                        ),
                    ]
                )
            else:
                dots.append(
                    Dot(
                        whitespace_after=parse_simple_whitespace(
                            config, child.whitespace_after
                        )
                    )
                )
        else:
            # This should be the dotted name, and we can't get more than
            # one, but lets be sure anyway
            if dotted_name is not None:
                raise CSTLogicError()
            dotted_name = child

    return ImportRelativePartial(relative=tuple(dots), module=dotted_name)


@with_production(
    "import_from",
    "'from' import_relative 'import' ('*' | '(' import_as_names ')' | import_as_names)",
)
def convert_import_from(config: ParserConfig, children: Sequence[Any]) -> Any:
    fromtoken, import_relative, importtoken, *importlist = children

    if len(importlist) == 1:
        (possible_star,) = importlist
        if isinstance(possible_star, Token):
            # Its a "*" import, so we must construct this node.
            names = ImportStar()
        else:
            # Its an import as names partial, grab the names from that.
            names = possible_star.names
        lpar = None
        rpar = None
    else:
        # Its an import as names partial with parens
        lpartoken, namespartial, rpartoken = importlist
        lpar = LeftParen(
            whitespace_after=parse_parenthesizable_whitespace(
                config, lpartoken.whitespace_after
            )
        )
        names = namespartial.names
        rpar = RightParen(
            whitespace_before=parse_parenthesizable_whitespace(
                config, rpartoken.whitespace_before
            )
        )

    # If we have a relative-only import, then we need to relocate the space
    # after the final dot to be owned by the import token.
    if len(import_relative.relative) > 0 and import_relative.module is None:
        whitespace_before_import = import_relative.relative[-1].whitespace_after
        relative = (
            *import_relative.relative[:-1],
            import_relative.relative[-1].with_changes(
                whitespace_after=SimpleWhitespace("")
            ),
        )
    else:
        whitespace_before_import = parse_simple_whitespace(
            config, importtoken.whitespace_before
        )
        relative = import_relative.relative

    return WithLeadingWhitespace(
        ImportFrom(
            whitespace_after_from=parse_simple_whitespace(
                config, fromtoken.whitespace_after
            ),
            relative=relative,
            module=import_relative.module,
            whitespace_before_import=whitespace_before_import,
            whitespace_after_import=parse_simple_whitespace(
                config, importtoken.whitespace_after
            ),
            lpar=lpar,
            names=names,
            rpar=rpar,
        ),
        fromtoken.whitespace_before,
    )


@with_production("import_as_name", "NAME ['as' NAME]")
def convert_import_as_name(config: ParserConfig, children: Sequence[Any]) -> Any:
    if len(children) == 1:
        (dotted_name,) = children
        return ImportAlias(name=Name(dotted_name.string), asname=None)
    else:
        dotted_name, astoken, name = children
        return ImportAlias(
            name=Name(dotted_name.string),
            asname=AsName(
                whitespace_before_as=parse_simple_whitespace(
                    config, astoken.whitespace_before
                ),
                whitespace_after_as=parse_simple_whitespace(
                    config, astoken.whitespace_after
                ),
                name=Name(name.string),
            ),
        )


@with_production("dotted_as_name", "dotted_name ['as' NAME]")
def convert_dotted_as_name(config: ParserConfig, children: Sequence[Any]) -> Any:
    if len(children) == 1:
        (dotted_name,) = children
        return ImportAlias(name=dotted_name, asname=None)
    else:
        dotted_name, astoken, name = children
        return ImportAlias(
            name=dotted_name,
            asname=AsName(
                whitespace_before_as=parse_parenthesizable_whitespace(
                    config, astoken.whitespace_before
                ),
                whitespace_after_as=parse_parenthesizable_whitespace(
                    config, astoken.whitespace_after
                ),
                name=Name(name.string),
            ),
        )


@with_production("import_as_names", "import_as_name (',' import_as_name)* [',']")
def convert_import_as_names(config: ParserConfig, children: Sequence[Any]) -> Any:
    return _gather_import_names(config, children)


@with_production("dotted_as_names", "dotted_as_name (',' dotted_as_name)*")
def convert_dotted_as_names(config: ParserConfig, children: Sequence[Any]) -> Any:
    return _gather_import_names(config, children)


def _gather_import_names(
    config: ParserConfig, children: Sequence[Any]
) -> ImportPartial:
    names = []
    for name, comma in grouper(children, 2):
        if comma is None:
            names.append(name)
        else:
            names.append(
                name.with_changes(
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

    return ImportPartial(names=names)


@with_production("dotted_name", "NAME ('.' NAME)*")
def convert_dotted_name(config: ParserConfig, children: Sequence[Any]) -> Any:
    left, *rest = children
    node = Name(left.string)

    for dot, right in grouper(rest, 2):
        node = Attribute(
            value=node,
            dot=Dot(
                whitespace_before=parse_parenthesizable_whitespace(
                    config, dot.whitespace_before
                ),
                whitespace_after=parse_parenthesizable_whitespace(
                    config, dot.whitespace_after
                ),
            ),
            attr=Name(right.string),
        )

    return node


@with_production("raise_stmt", "'raise' [test ['from' test]]")
def convert_raise_stmt(config: ParserConfig, children: Sequence[Any]) -> Any:
    if len(children) == 1:
        (raise_token,) = children
        whitespace_after_raise = MaybeSentinel.DEFAULT
        exc = None
        cause = None
    elif len(children) == 2:
        (raise_token, test) = children
        whitespace_after_raise = parse_simple_whitespace(config, test.whitespace_before)
        exc = test.value
        cause = None
    elif len(children) == 4:
        (raise_token, test, from_token, source) = children
        whitespace_after_raise = parse_simple_whitespace(config, test.whitespace_before)
        exc = test.value
        cause = From(
            whitespace_before_from=parse_simple_whitespace(
                config, from_token.whitespace_before
            ),
            whitespace_after_from=parse_simple_whitespace(
                config, source.whitespace_before
            ),
            item=source.value,
        )
    else:
        raise CSTLogicError()

    return WithLeadingWhitespace(
        Raise(whitespace_after_raise=whitespace_after_raise, exc=exc, cause=cause),
        raise_token.whitespace_before,
    )


def _construct_nameitems(config: ParserConfig, names: Sequence[Any]) -> List[NameItem]:
    nameitems: List[NameItem] = []
    for name, maybe_comma in grouper(names, 2):
        if maybe_comma is None:
            nameitems.append(NameItem(Name(name.string)))
        else:
            nameitems.append(
                NameItem(
                    Name(name.string),
                    comma=Comma(
                        whitespace_before=parse_simple_whitespace(
                            config, maybe_comma.whitespace_before
                        ),
                        whitespace_after=parse_simple_whitespace(
                            config, maybe_comma.whitespace_after
                        ),
                    ),
                )
            )
    return nameitems


@with_production("global_stmt", "'global' NAME (',' NAME)*")
def convert_global_stmt(config: ParserConfig, children: Sequence[Any]) -> Any:
    (global_token, *names) = children
    return WithLeadingWhitespace(
        Global(
            names=tuple(_construct_nameitems(config, names)),
            whitespace_after_global=parse_simple_whitespace(
                config, names[0].whitespace_before
            ),
        ),
        global_token.whitespace_before,
    )


@with_production("nonlocal_stmt", "'nonlocal' NAME (',' NAME)*")
def convert_nonlocal_stmt(config: ParserConfig, children: Sequence[Any]) -> Any:
    (nonlocal_token, *names) = children
    return WithLeadingWhitespace(
        Nonlocal(
            names=tuple(_construct_nameitems(config, names)),
            whitespace_after_nonlocal=parse_simple_whitespace(
                config, names[0].whitespace_before
            ),
        ),
        nonlocal_token.whitespace_before,
    )


@with_production("assert_stmt", "'assert' test [',' test]")
def convert_assert_stmt(config: ParserConfig, children: Sequence[Any]) -> Any:
    if len(children) == 2:
        (assert_token, test) = children
        assert_node = Assert(
            whitespace_after_assert=parse_simple_whitespace(
                config, test.whitespace_before
            ),
            test=test.value,
            msg=None,
        )
    else:
        (assert_token, test, comma_token, msg) = children
        assert_node = Assert(
            whitespace_after_assert=parse_simple_whitespace(
                config, test.whitespace_before
            ),
            test=test.value,
            comma=Comma(
                whitespace_before=parse_simple_whitespace(
                    config, comma_token.whitespace_before
                ),
                whitespace_after=parse_simple_whitespace(config, msg.whitespace_before),
            ),
            msg=msg.value,
        )

    return WithLeadingWhitespace(assert_node, assert_token.whitespace_before)


@with_production(
    "compound_stmt",
    ("if_stmt | while_stmt | asyncable_stmt | try_stmt | classdef | decorated"),
)
def convert_compound_stmt(config: ParserConfig, children: Sequence[Any]) -> Any:
    (stmt,) = children
    return stmt


@with_production(
    "if_stmt", "'if' test ':' suite [if_stmt_elif|if_stmt_else]", version="<=3.7"
)
@with_production(
    "if_stmt",
    "'if' namedexpr_test ':' suite [if_stmt_elif|if_stmt_else]",
    version=">=3.8",
)
def convert_if_stmt(config: ParserConfig, children: Sequence[Any]) -> Any:
    if_tok, test, colon_tok, suite, *tail = children

    if len(tail) > 0:
        (orelse,) = tail
    else:
        orelse = None

    return If(
        leading_lines=parse_empty_lines(config, if_tok.whitespace_before),
        whitespace_before_test=parse_simple_whitespace(config, if_tok.whitespace_after),
        test=test.value,
        whitespace_after_test=parse_simple_whitespace(
            config, colon_tok.whitespace_before
        ),
        body=suite,
        orelse=orelse,
    )


@with_production(
    "if_stmt_elif", "'elif' test ':' suite [if_stmt_elif|if_stmt_else]", version="<=3.7"
)
@with_production(
    "if_stmt_elif",
    "'elif' namedexpr_test ':' suite [if_stmt_elif|if_stmt_else]",
    version=">=3.8",
)
def convert_if_stmt_elif(config: ParserConfig, children: Sequence[Any]) -> Any:
    # this behaves exactly the same as `convert_if_stmt`, except that the leading token
    # has a different string value.
    return convert_if_stmt(config, children)


@with_production("if_stmt_else", "'else' ':' suite")
def convert_if_stmt_else(config: ParserConfig, children: Sequence[Any]) -> Any:
    else_tok, colon_tok, suite = children
    return Else(
        leading_lines=parse_empty_lines(config, else_tok.whitespace_before),
        whitespace_before_colon=parse_simple_whitespace(
            config, colon_tok.whitespace_before
        ),
        body=suite,
    )


@with_production(
    "while_stmt", "'while' test ':' suite ['else' ':' suite]", version="<=3.7"
)
@with_production(
    "while_stmt", "'while' namedexpr_test ':' suite ['else' ':' suite]", version=">=3.8"
)
def convert_while_stmt(config: ParserConfig, children: Sequence[Any]) -> Any:
    while_token, test, while_colon_token, while_suite, *else_block = children

    if len(else_block) > 0:
        (else_token, else_colon_token, else_suite) = else_block
        orelse = Else(
            leading_lines=parse_empty_lines(config, else_token.whitespace_before),
            whitespace_before_colon=parse_simple_whitespace(
                config, else_colon_token.whitespace_before
            ),
            body=else_suite,
        )
    else:
        orelse = None

    return While(
        leading_lines=parse_empty_lines(config, while_token.whitespace_before),
        whitespace_after_while=parse_simple_whitespace(
            config, while_token.whitespace_after
        ),
        test=test.value,
        whitespace_before_colon=parse_simple_whitespace(
            config, while_colon_token.whitespace_before
        ),
        body=while_suite,
        orelse=orelse,
    )


@with_production(
    "for_stmt", "'for' exprlist 'in' testlist ':' suite ['else' ':' suite]"
)
def convert_for_stmt(config: ParserConfig, children: Sequence[Any]) -> Any:
    (
        for_token,
        expr,
        in_token,
        test,
        for_colon_token,
        for_suite,
        *else_block,
    ) = children

    if len(else_block) > 0:
        (else_token, else_colon_token, else_suite) = else_block
        orelse = Else(
            leading_lines=parse_empty_lines(config, else_token.whitespace_before),
            whitespace_before_colon=parse_simple_whitespace(
                config, else_colon_token.whitespace_before
            ),
            body=else_suite,
        )
    else:
        orelse = None

    return WithLeadingWhitespace(
        For(
            whitespace_after_for=parse_simple_whitespace(
                config, for_token.whitespace_after
            ),
            target=expr.value,
            whitespace_before_in=parse_simple_whitespace(
                config, in_token.whitespace_before
            ),
            whitespace_after_in=parse_simple_whitespace(
                config, in_token.whitespace_after
            ),
            iter=test.value,
            whitespace_before_colon=parse_simple_whitespace(
                config, for_colon_token.whitespace_before
            ),
            body=for_suite,
            orelse=orelse,
        ),
        for_token.whitespace_before,
    )


@with_production(
    "try_stmt",
    "('try' ':' suite ((except_clause ':' suite)+ ['else' ':' suite] ['finally' ':' suite] | 'finally' ':' suite))",
)
def convert_try_stmt(config: ParserConfig, children: Sequence[Any]) -> Any:
    trytoken, try_colon_token, try_suite, *rest = children
    handlers: List[ExceptHandler] = []
    orelse: Optional[Else] = None
    finalbody: Optional[Finally] = None

    for clause, colon_token, suite in grouper(rest, 3):
        if isinstance(clause, Token):
            if clause.string == "else":
                if orelse is not None:
                    raise CSTLogicError("Logic error!")
                orelse = Else(
                    leading_lines=parse_empty_lines(config, clause.whitespace_before),
                    whitespace_before_colon=parse_simple_whitespace(
                        config, colon_token.whitespace_before
                    ),
                    body=suite,
                )
            elif clause.string == "finally":
                if finalbody is not None:
                    raise CSTLogicError("Logic error!")
                finalbody = Finally(
                    leading_lines=parse_empty_lines(config, clause.whitespace_before),
                    whitespace_before_colon=parse_simple_whitespace(
                        config, colon_token.whitespace_before
                    ),
                    body=suite,
                )
            else:
                raise CSTLogicError("Logic error!")
        elif isinstance(clause, ExceptClausePartial):
            handlers.append(
                ExceptHandler(
                    body=suite,
                    type=clause.type,
                    name=clause.name,
                    leading_lines=clause.leading_lines,
                    whitespace_after_except=clause.whitespace_after_except,
                    whitespace_before_colon=parse_simple_whitespace(
                        config, colon_token.whitespace_before
                    ),
                )
            )
        else:
            raise CSTLogicError("Logic error!")

    return Try(
        leading_lines=parse_empty_lines(config, trytoken.whitespace_before),
        whitespace_before_colon=parse_simple_whitespace(
            config, try_colon_token.whitespace_before
        ),
        body=try_suite,
        handlers=tuple(handlers),
        orelse=orelse,
        finalbody=finalbody,
    )


@with_production("except_clause", "'except' [test ['as' NAME]]")
def convert_except_clause(config: ParserConfig, children: Sequence[Any]) -> Any:
    if len(children) == 1:
        (except_token,) = children
        whitespace_after_except = SimpleWhitespace("")
        test = None
        name = None
    elif len(children) == 2:
        (except_token, test_node) = children
        whitespace_after_except = parse_simple_whitespace(
            config, except_token.whitespace_after
        )
        test = test_node.value
        name = None
    else:
        (except_token, test_node, as_token, name_token) = children
        whitespace_after_except = parse_simple_whitespace(
            config, except_token.whitespace_after
        )
        test = test_node.value
        name = AsName(
            whitespace_before_as=parse_simple_whitespace(
                config, as_token.whitespace_before
            ),
            whitespace_after_as=parse_simple_whitespace(
                config, as_token.whitespace_after
            ),
            name=Name(name_token.string),
        )

    return ExceptClausePartial(
        leading_lines=parse_empty_lines(config, except_token.whitespace_before),
        whitespace_after_except=whitespace_after_except,
        type=test,
        name=name,
    )


@with_production(
    "with_stmt", "'with' with_item (',' with_item)*  ':' suite", version=">=3.1"
)
@with_production("with_stmt", "'with' with_item ':' suite", version="<3.1")
def convert_with_stmt(config: ParserConfig, children: Sequence[Any]) -> Any:
    (with_token, *items, colon_token, suite) = children
    item_nodes: List[WithItem] = []

    for with_item, maybe_comma in grouper(items, 2):
        if maybe_comma is not None:
            item_nodes.append(
                with_item.with_changes(
                    comma=Comma(
                        whitespace_before=parse_parenthesizable_whitespace(
                            config, maybe_comma.whitespace_before
                        ),
                        whitespace_after=parse_parenthesizable_whitespace(
                            config, maybe_comma.whitespace_after
                        ),
                    )
                )
            )
        else:
            item_nodes.append(with_item)

    return WithLeadingWhitespace(
        With(
            whitespace_after_with=parse_simple_whitespace(
                config, with_token.whitespace_after
            ),
            items=tuple(item_nodes),
            whitespace_before_colon=parse_simple_whitespace(
                config, colon_token.whitespace_before
            ),
            body=suite,
        ),
        with_token.whitespace_before,
    )


@with_production("with_item", "test ['as' expr]")
def convert_with_item(config: ParserConfig, children: Sequence[Any]) -> Any:
    if len(children) == 3:
        (test, as_token, expr_node) = children
        test_node = test.value
        asname = AsName(
            whitespace_before_as=parse_simple_whitespace(
                config, as_token.whitespace_before
            ),
            whitespace_after_as=parse_simple_whitespace(
                config, as_token.whitespace_after
            ),
            name=expr_node.value,
        )
    else:
        (test,) = children
        test_node = test.value
        asname = None

    return WithItem(item=test_node, asname=asname)


def _extract_async(
    config: ParserConfig, children: Sequence[Any]
) -> Tuple[List[EmptyLine], Optional[Asynchronous], Any]:
    if len(children) == 1:
        (stmt,) = children

        whitespace_before = stmt.whitespace_before
        asyncnode = None
    else:
        asynctoken, stmt = children

        whitespace_before = asynctoken.whitespace_before
        asyncnode = Asynchronous(
            whitespace_after=parse_simple_whitespace(
                config, asynctoken.whitespace_after
            )
        )

    return (parse_empty_lines(config, whitespace_before), asyncnode, stmt.value)


@with_production("asyncable_funcdef", "[ASYNC] funcdef", version=">=3.5")
@with_production("asyncable_funcdef", "funcdef", version="<3.5")
def convert_asyncable_funcdef(config: ParserConfig, children: Sequence[Any]) -> Any:
    leading_lines, asyncnode, funcdef = _extract_async(config, children)

    return funcdef.with_changes(
        asynchronous=asyncnode, leading_lines=leading_lines, lines_after_decorators=()
    )


@with_production("funcdef", "'def' NAME parameters [funcdef_annotation] ':' suite")
def convert_funcdef(config: ParserConfig, children: Sequence[Any]) -> Any:
    defnode, namenode, param_partial, *annotation, colon, suite = children

    # If the trailing paremeter doesn't have a comma, then it owns the trailing
    # whitespace before the rpar. Otherwise, the comma owns it (and will have
    # already parsed it). We don't check/update ParamStar because if it exists
    # then we are guaranteed have at least one kwonly_param.
    parameters = param_partial.params
    if parameters.star_kwarg is not None:
        if parameters.star_kwarg.comma == MaybeSentinel.DEFAULT:
            parameters = parameters.with_changes(
                star_kwarg=parameters.star_kwarg.with_changes(
                    whitespace_after_param=param_partial.rpar.whitespace_before
                )
            )
    elif parameters.kwonly_params:
        if parameters.kwonly_params[-1].comma == MaybeSentinel.DEFAULT:
            parameters = parameters.with_changes(
                kwonly_params=(
                    *parameters.kwonly_params[:-1],
                    parameters.kwonly_params[-1].with_changes(
                        whitespace_after_param=param_partial.rpar.whitespace_before
                    ),
                )
            )
    elif isinstance(parameters.star_arg, Param):
        if parameters.star_arg.comma == MaybeSentinel.DEFAULT:
            parameters = parameters.with_changes(
                star_arg=parameters.star_arg.with_changes(
                    whitespace_after_param=param_partial.rpar.whitespace_before
                )
            )
    elif parameters.params:
        if parameters.params[-1].comma == MaybeSentinel.DEFAULT:
            parameters = parameters.with_changes(
                params=(
                    *parameters.params[:-1],
                    parameters.params[-1].with_changes(
                        whitespace_after_param=param_partial.rpar.whitespace_before
                    ),
                )
            )

    return WithLeadingWhitespace(
        FunctionDef(
            whitespace_after_def=parse_simple_whitespace(
                config, defnode.whitespace_after
            ),
            name=Name(namenode.string),
            whitespace_after_name=parse_simple_whitespace(
                config, namenode.whitespace_after
            ),
            whitespace_before_params=param_partial.lpar.whitespace_after,
            params=parameters,
            returns=None if not annotation else annotation[0],
            whitespace_before_colon=parse_simple_whitespace(
                config, colon.whitespace_before
            ),
            body=suite,
        ),
        defnode.whitespace_before,
    )


@with_production("parameters", "'(' [typedargslist] ')'")
def convert_parameters(config: ParserConfig, children: Sequence[Any]) -> Any:
    lpar, *paramlist, rpar = children
    return FuncdefPartial(
        lpar=LeftParen(
            whitespace_after=parse_parenthesizable_whitespace(
                config, lpar.whitespace_after
            )
        ),
        params=Parameters() if not paramlist else paramlist[0],
        rpar=RightParen(
            whitespace_before=parse_parenthesizable_whitespace(
                config, rpar.whitespace_before
            )
        ),
    )


@with_production("funcdef_annotation", "'->' test")
def convert_funcdef_annotation(config: ParserConfig, children: Sequence[Any]) -> Any:
    arrow, typehint = children
    return Annotation(
        whitespace_before_indicator=parse_parenthesizable_whitespace(
            config, arrow.whitespace_before
        ),
        whitespace_after_indicator=parse_parenthesizable_whitespace(
            config, arrow.whitespace_after
        ),
        annotation=typehint.value,
    )


@with_production("classdef", "'class' NAME ['(' [arglist] ')'] ':' suite")
def convert_classdef(config: ParserConfig, children: Sequence[Any]) -> Any:
    classdef, name, *arglist, colon, suite = children

    # First, parse out the comments and empty lines before the statement.
    leading_lines = parse_empty_lines(config, classdef.whitespace_before)

    # Compute common whitespace and nodes
    whitespace_after_class = parse_simple_whitespace(config, classdef.whitespace_after)
    namenode = Name(name.string)
    whitespace_after_name = parse_simple_whitespace(config, name.whitespace_after)

    # Now, construct the classdef node itself
    if not arglist:
        # No arglist, so no arguments to this class
        return ClassDef(
            leading_lines=leading_lines,
            lines_after_decorators=(),
            whitespace_after_class=whitespace_after_class,
            name=namenode,
            whitespace_after_name=whitespace_after_name,
            body=suite,
        )
    else:
        # Unwrap arglist partial, because its valid to not have any
        lpar, *args, rpar = arglist
        args = args[0].args if args else []

        bases: List[Arg] = []
        keywords: List[Arg] = []

        current_arg = bases
        for arg in args:
            if arg.star == "**" or arg.keyword is not None:
                current_arg = keywords
            # Some quick validation
            if current_arg is keywords and (
                arg.star == "*" or (arg.star == "" and arg.keyword is None)
            ):
                raise PartialParserSyntaxError(
                    "Positional argument follows keyword argument."
                )
            current_arg.append(arg)

        return ClassDef(
            leading_lines=leading_lines,
            lines_after_decorators=(),
            whitespace_after_class=whitespace_after_class,
            name=namenode,
            whitespace_after_name=whitespace_after_name,
            lpar=LeftParen(
                whitespace_after=parse_parenthesizable_whitespace(
                    config, lpar.whitespace_after
                )
            ),
            bases=bases,
            keywords=keywords,
            rpar=RightParen(
                whitespace_before=parse_parenthesizable_whitespace(
                    config, rpar.whitespace_before
                )
            ),
            whitespace_before_colon=parse_simple_whitespace(
                config, colon.whitespace_before
            ),
            body=suite,
        )


@with_production("decorator", "'@' dotted_name [ '(' [arglist] ')' ] NEWLINE")
def convert_decorator(config: ParserConfig, children: Sequence[Any]) -> Any:
    atsign, name, *arglist, newline = children
    if not arglist:
        # This is either a name or an attribute node, so just extract it.
        decoratornode = name
    else:
        # This needs to be converted into a call node, and we have the
        # arglist partial.
        lpar, *args, rpar = arglist
        args = args[0].args if args else []

        # If the trailing argument doesn't have a comma, then it owns the
        # trailing whitespace before the rpar. Otherwise, the comma owns
        # it.
        if len(args) > 0 and args[-1].comma == MaybeSentinel.DEFAULT:
            args[-1] = args[-1].with_changes(
                whitespace_after_arg=parse_parenthesizable_whitespace(
                    config, rpar.whitespace_before
                )
            )

        decoratornode = Call(
            func=name,
            whitespace_after_func=parse_simple_whitespace(
                config, lpar.whitespace_before
            ),
            whitespace_before_args=parse_parenthesizable_whitespace(
                config, lpar.whitespace_after
            ),
            args=tuple(args),
        )

    return Decorator(
        leading_lines=parse_empty_lines(config, atsign.whitespace_before),
        whitespace_after_at=parse_simple_whitespace(config, atsign.whitespace_after),
        decorator=decoratornode,
        trailing_whitespace=newline,
    )


@with_production("decorators", "decorator+")
def convert_decorators(config: ParserConfig, children: Sequence[Any]) -> Any:
    return DecoratorPartial(decorators=children)


@with_production("decorated", "decorators (classdef | asyncable_funcdef)")
def convert_decorated(config: ParserConfig, children: Sequence[Any]) -> Any:
    partial, class_or_func = children

    # First, split up the spacing on the first decorator
    leading_lines = partial.decorators[0].leading_lines

    # Now, redistribute ownership of the whitespace
    decorators = (
        partial.decorators[0].with_changes(leading_lines=()),
        *partial.decorators[1:],
    )

    # Now, modify the original function or class to add the decorators.
    return class_or_func.with_changes(
        leading_lines=leading_lines,
        # pyre-fixme[60]: Concatenation not yet support for multiple variadic
        #  tuples: `*class_or_func.leading_lines,
        #  *class_or_func.lines_after_decorators`.
        # pyre-fixme[60]: Expected to unpack an iterable, but got `unknown`.
        lines_after_decorators=(
            *class_or_func.leading_lines,
            *class_or_func.lines_after_decorators,
        ),
        decorators=decorators,
    )


@with_production(
    "asyncable_stmt", "[ASYNC] (funcdef | with_stmt | for_stmt)", version=">=3.5"
)
@with_production("asyncable_stmt", "funcdef | with_stmt | for_stmt", version="<3.5")
def convert_asyncable_stmt(config: ParserConfig, children: Sequence[Any]) -> Any:
    leading_lines, asyncnode, stmtnode = _extract_async(config, children)
    if isinstance(stmtnode, FunctionDef):
        return stmtnode.with_changes(
            asynchronous=asyncnode,
            leading_lines=leading_lines,
            lines_after_decorators=(),
        )
    elif isinstance(stmtnode, With):
        return stmtnode.with_changes(
            asynchronous=asyncnode, leading_lines=leading_lines
        )
    elif isinstance(stmtnode, For):
        return stmtnode.with_changes(
            asynchronous=asyncnode, leading_lines=leading_lines
        )
    else:
        raise CSTLogicError("Logic error!")


@with_production("suite", "simple_stmt_suite | indented_suite")
def convert_suite(config: ParserConfig, children: Sequence[Any]) -> Any:
    (suite,) = children
    return suite


@with_production("indented_suite", "NEWLINE INDENT stmt+ DEDENT")
def convert_indented_suite(config: ParserConfig, children: Sequence[Any]) -> Any:
    newline, indent, *stmts, dedent = children
    return IndentedBlock(
        header=newline,
        indent=(
            None
            if indent.relative_indent == config.default_indent
            else indent.relative_indent
        ),
        body=stmts,
        # We want to be able to only keep comments in the footer that are actually for
        # this IndentedBlock. We do so by assuming that lines which are indented to the
        # same level as the block itself are comments that go at the footer of the
        # block. Comments that are indented to less than this indent are assumed to
        # belong to the next line of code. We override the indent here because the
        # dedent node's absolute indent is the resulting indentation after the dedent
        # is performed. Its this way because the whitespace state for both the dedent's
        # whitespace_after and the next BaseCompoundStatement's whitespace_before is
        # shared. This allows us to partially parse here and parse the rest of the
        # whitespace and comments on the next line, effectively making sure that
        # comments are attached to the correct node.
        footer=parse_empty_lines(
            config,
            dedent.whitespace_after,
            override_absolute_indent=indent.whitespace_before.absolute_indent,
        ),
    )
