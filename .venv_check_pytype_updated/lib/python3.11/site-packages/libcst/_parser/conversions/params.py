# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
# pyre-unsafe

from typing import Any, List, Optional, Sequence, Union

from libcst import CSTLogicError
from libcst._exceptions import PartialParserSyntaxError
from libcst._maybe_sentinel import MaybeSentinel
from libcst._nodes.expression import (
    Annotation,
    Name,
    Param,
    Parameters,
    ParamSlash,
    ParamStar,
)
from libcst._nodes.op import AssignEqual, Comma
from libcst._parser.custom_itertools import grouper
from libcst._parser.production_decorator import with_production
from libcst._parser.types.config import ParserConfig
from libcst._parser.types.partials import ParamStarPartial
from libcst._parser.whitespace_parser import parse_parenthesizable_whitespace


@with_production(  # noqa: C901: too complex
    "typedargslist",
    """(
      (tfpdef_assign (',' tfpdef_assign)* ',' tfpdef_posind [',' [ tfpdef_assign (
            ',' tfpdef_assign)* [',' [
            tfpdef_star (',' tfpdef_assign)* [',' [tfpdef_starstar [',']]]
          | tfpdef_starstar [',']]]
      | tfpdef_star (',' tfpdef_assign)* [',' [tfpdef_starstar [',']]]
      | tfpdef_starstar [',']]] )
    |  (tfpdef_assign (',' tfpdef_assign)* [',' [
       tfpdef_star (',' tfpdef_assign)* [',' [tfpdef_starstar [',']]]
      | tfpdef_starstar [',']]]
      | tfpdef_star (',' tfpdef_assign)* [',' [tfpdef_starstar [',']]]
      | tfpdef_starstar [','])
    )""",
    version=">=3.8",
)
@with_production(  # noqa: C901: too complex
    "typedargslist",
    (
        "(tfpdef_assign (',' tfpdef_assign)* "
        + "[',' [tfpdef_star (',' tfpdef_assign)* [',' [tfpdef_starstar [',']]] | tfpdef_starstar [',']]]"
        + "| tfpdef_star (',' tfpdef_assign)* [',' [tfpdef_starstar [',']]] | tfpdef_starstar [','])"
    ),
    version=">=3.6,<=3.7",
)
@with_production(  # noqa: C901: too complex
    "typedargslist",
    (
        "(tfpdef_assign (',' tfpdef_assign)* "
        + "[',' [tfpdef_star (',' tfpdef_assign)* [',' tfpdef_starstar] | tfpdef_starstar]]"
        + "| tfpdef_star (',' tfpdef_assign)* [',' tfpdef_starstar] | tfpdef_starstar)"
    ),
    version="<=3.5",
)
@with_production(
    "varargslist",
    """vfpdef_assign (',' vfpdef_assign)* ',' vfpdef_posind [',' [ (vfpdef_assign (',' vfpdef_assign)* [',' [
            vfpdef_star (',' vfpdef_assign)* [',' [vfpdef_starstar [',']]]
          | vfpdef_starstar [',']]]
      | vfpdef_star (',' vfpdef_assign)* [',' [vfpdef_starstar [',']]]
      | vfpdef_starstar [',']) ]] | (vfpdef_assign (',' vfpdef_assign)* [',' [
            vfpdef_star (',' vfpdef_assign)* [',' [vfpdef_starstar [',']]]
          | vfpdef_starstar [',']]]
      | vfpdef_star (',' vfpdef_assign)* [',' [vfpdef_starstar [',']]]
      | vfpdef_starstar [',']
    )""",
    version=">=3.8",
)
@with_production(
    "varargslist",
    (
        "(vfpdef_assign (',' vfpdef_assign)* "
        + "[',' [vfpdef_star (',' vfpdef_assign)* [',' [vfpdef_starstar [',']]] | vfpdef_starstar [',']]]"
        + "| vfpdef_star (',' vfpdef_assign)* [',' [vfpdef_starstar [',']]] | vfpdef_starstar [','])"
    ),
    version=">=3.6,<=3.7",
)
@with_production(
    "varargslist",
    (
        "(vfpdef_assign (',' vfpdef_assign)* "
        + "[',' [vfpdef_star (',' vfpdef_assign)* [',' vfpdef_starstar] | vfpdef_starstar]]"
        + "| vfpdef_star (',' vfpdef_assign)* [',' vfpdef_starstar] | vfpdef_starstar)"
    ),
    version="<=3.5",
)
def convert_argslist(  # noqa: C901
    config: ParserConfig, children: Sequence[Any]
) -> Any:
    posonly_params: List[Param] = []
    posonly_ind: Union[ParamSlash, MaybeSentinel] = MaybeSentinel.DEFAULT
    params: List[Param] = []
    seen_default: bool = False
    star_arg: Union[Param, ParamStar, MaybeSentinel] = MaybeSentinel.DEFAULT
    kwonly_params: List[Param] = []
    star_kwarg: Optional[Param] = None

    def add_param(
        current_param: Optional[List[Param]], param: Union[Param, ParamStar]
    ) -> Optional[List[Param]]:
        nonlocal star_arg
        nonlocal star_kwarg
        nonlocal seen_default
        nonlocal posonly_params
        nonlocal posonly_ind
        nonlocal params

        if isinstance(param, ParamStar):
            # Only can add this if we don't already have a "*" or a "*param".
            if current_param is params:
                star_arg = param
                current_param = kwonly_params
            else:
                # Example code:
                #     def fn(*abc, *): ...
                # This should be unreachable, the grammar already disallows it.
                raise ValueError(
                    "Cannot have multiple star ('*') markers in a single argument "
                    + "list."
                )
        elif isinstance(param, ParamSlash):
            # Only can add this if we don't already have a "/" or a "*" or a "*param".
            if current_param is params and len(posonly_params) == 0:
                posonly_ind = param
                posonly_params = params
                params = []
                current_param = params
            else:
                # Example code:
                # def fn(foo, /, *, /, bar): ...
                # This should be unreachable, the grammar already disallows it.
                raise ValueError(
                    "Cannot have multiple slash ('/') markers in a single argument "
                    + "list."
                )
        elif isinstance(param.star, str) and param.star == "" and param.default is None:
            # Can only add this if we're in the params or kwonly_params section
            if current_param is params and not seen_default:
                params.append(param)
            elif current_param is kwonly_params:
                kwonly_params.append(param)
            else:
                # Example code:
                #     def fn(first=None, second): ...
                # This code is reachable, so we should use a PartialParserSyntaxError.
                raise PartialParserSyntaxError(
                    "Cannot have a non-default argument following a default argument."
                )
        elif (
            isinstance(param.star, str)
            and param.star == ""
            and param.default is not None
        ):
            # Can only add this if we're not yet at star args.
            if current_param is params:
                seen_default = True
                params.append(param)
            elif current_param is kwonly_params:
                kwonly_params.append(param)
            else:
                # Example code:
                #     def fn(**kwargs, trailing=None)
                # This should be unreachable, the grammar already disallows it.
                raise ValueError("Cannot have any arguments after a kwargs expansion.")
        elif (
            isinstance(param.star, str) and param.star == "*" and param.default is None
        ):
            # Can only add this if we're in params, since we only allow one of
            # "*" or "*param".
            if current_param is params:
                star_arg = param
                current_param = kwonly_params
            else:
                # Example code:
                #     def fn(*first, *second): ...
                # This should be unreachable, the grammar already disallows it.
                raise ValueError(
                    "Expected a keyword argument but found a starred positional "
                    + "argument expansion."
                )
        elif (
            isinstance(param.star, str) and param.star == "**" and param.default is None
        ):
            # Can add this in all cases where we don't have a star_kwarg
            # yet.
            if current_param is not None:
                star_kwarg = param
                current_param = None
            else:
                # Example code:
                #     def fn(**first, **second)
                # This should be unreachable, the grammar already disallows it.
                raise ValueError(
                    "Multiple starred keyword argument expansions are not allowed in a "
                    + "single argument list"
                )
        else:
            # The state machine should never end up here.
            raise CSTLogicError("Logic error!")

        return current_param

    # The parameter list we are adding to
    current: Optional[List[Param]] = params

    # We should have every other item in the group as a param or a comma by now,
    # so split them up, add commas and then put them in the appropriate group.
    for parameter, comma in grouper(children, 2):
        if comma is None:
            if isinstance(parameter, ParamStarPartial):
                # Example:
                #     def fn(abc, *): ...
                #
                # There's also the case where we have bare * with a trailing comma.
                # That's handled later.
                #
                # It's not valid to construct a ParamStar object without a comma, so we
                # need to catch the non-comma case separately.
                raise PartialParserSyntaxError(
                    "Named (keyword) arguments must follow a bare *."
                )
            else:
                current = add_param(current, parameter)
        else:
            comma = Comma(
                whitespace_before=parse_parenthesizable_whitespace(
                    config, comma.whitespace_before
                ),
                whitespace_after=parse_parenthesizable_whitespace(
                    config, comma.whitespace_after
                ),
            )
            if isinstance(parameter, ParamStarPartial):
                current = add_param(current, ParamStar(comma=comma))
            else:
                current = add_param(current, parameter.with_changes(comma=comma))

    if isinstance(star_arg, ParamStar) and len(kwonly_params) == 0:
        # Example:
        #     def fn(abc, *,): ...
        #
        # This will raise a validation error, but we want to make sure to raise a syntax
        # error instead.
        #
        # The case where there's no trailing comma is already handled by this point, so
        # this conditional is only for the case where we have a trailing comma.
        raise PartialParserSyntaxError(
            "Named (keyword) arguments must follow a bare *."
        )

    return Parameters(
        posonly_params=tuple(posonly_params),
        posonly_ind=posonly_ind,
        params=tuple(params),
        star_arg=star_arg,
        kwonly_params=tuple(kwonly_params),
        star_kwarg=star_kwarg,
    )


@with_production("tfpdef_star", "'*' [tfpdef]")
@with_production("vfpdef_star", "'*' [vfpdef]")
def convert_fpdef_star(config: ParserConfig, children: Sequence[Any]) -> Any:
    if len(children) == 1:
        (star,) = children
        return ParamStarPartial()
    else:
        star, param = children
        return param.with_changes(
            star=star.string,
            whitespace_after_star=parse_parenthesizable_whitespace(
                config, star.whitespace_after
            ),
        )


@with_production("tfpdef_starstar", "'**' tfpdef")
@with_production("vfpdef_starstar", "'**' vfpdef")
def convert_fpdef_starstar(config: ParserConfig, children: Sequence[Any]) -> Any:
    starstar, param = children
    return param.with_changes(
        star=starstar.string,
        whitespace_after_star=parse_parenthesizable_whitespace(
            config, starstar.whitespace_after
        ),
    )


@with_production("tfpdef_assign", "tfpdef ['=' test]")
@with_production("vfpdef_assign", "vfpdef ['=' test]")
def convert_fpdef_assign(config: ParserConfig, children: Sequence[Any]) -> Any:
    if len(children) == 1:
        (child,) = children
        return child

    param, equal, default = children
    return param.with_changes(
        equal=AssignEqual(
            whitespace_before=parse_parenthesizable_whitespace(
                config, equal.whitespace_before
            ),
            whitespace_after=parse_parenthesizable_whitespace(
                config, equal.whitespace_after
            ),
        ),
        default=default.value,
    )


@with_production("tfpdef", "NAME [':' test]")
@with_production("vfpdef", "NAME")
def convert_fpdef(config: ParserConfig, children: Sequence[Any]) -> Any:
    if len(children) == 1:
        # This is just a parameter
        (child,) = children
        namenode = Name(child.string)
        annotation = None
    else:
        # This is a parameter with a type hint
        name, colon, typehint = children
        namenode = Name(name.string)
        annotation = Annotation(
            whitespace_before_indicator=parse_parenthesizable_whitespace(
                config, colon.whitespace_before
            ),
            whitespace_after_indicator=parse_parenthesizable_whitespace(
                config, colon.whitespace_after
            ),
            annotation=typehint.value,
        )

    return Param(star="", name=namenode, annotation=annotation, default=None)


@with_production("tfpdef_posind", "'/'")
@with_production("vfpdef_posind", "'/'")
def convert_fpdef_slash(config: ParserConfig, children: Sequence[Any]) -> Any:
    return ParamSlash()
