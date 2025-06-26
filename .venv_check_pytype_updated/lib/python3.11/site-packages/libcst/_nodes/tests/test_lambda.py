# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Callable, Optional

import libcst as cst
from libcst import parse_expression
from libcst._nodes.tests.base import CSTNodeTest
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider


class LambdaCreationTest(CSTNodeTest):
    @data_provider(
        (
            # Simple lambda
            (cst.Lambda(cst.Parameters(), cst.Integer("5")), "lambda: 5"),
            # Test basic positional only params
            {
                "node": cst.Lambda(
                    cst.Parameters(
                        posonly_params=(
                            cst.Param(cst.Name("bar")),
                            cst.Param(cst.Name("baz")),
                        )
                    ),
                    cst.Integer("5"),
                ),
                "code": "lambda bar, baz, /: 5",
            },
            # Test basic positional only params with extra trailing whitespace
            {
                "node": cst.Lambda(
                    cst.Parameters(
                        posonly_params=(
                            cst.Param(cst.Name("bar")),
                            cst.Param(cst.Name("baz")),
                        ),
                        posonly_ind=cst.ParamSlash(
                            whitespace_after=cst.SimpleWhitespace(" ")
                        ),
                    ),
                    cst.Integer("5"),
                ),
                "code": "lambda bar, baz, / : 5",
            },
            # Test basic positional params
            (
                cst.Lambda(
                    cst.Parameters(
                        params=(cst.Param(cst.Name("bar")), cst.Param(cst.Name("baz")))
                    ),
                    cst.Integer("5"),
                ),
                "lambda bar, baz: 5",
            ),
            # Test basic positional default params
            (
                cst.Lambda(
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("bar"), default=cst.SimpleString('"one"')
                            ),
                            cst.Param(cst.Name("baz"), default=cst.Integer("5")),
                        )
                    ),
                    cst.Integer("5"),
                ),
                'lambda bar = "one", baz = 5: 5',
            ),
            # Mixed positional and default params.
            (
                cst.Lambda(
                    cst.Parameters(
                        params=(
                            cst.Param(cst.Name("bar")),
                            cst.Param(cst.Name("baz"), default=cst.Integer("5")),
                        )
                    ),
                    cst.Integer("5"),
                ),
                "lambda bar, baz = 5: 5",
            ),
            # Test kwonly params
            (
                cst.Lambda(
                    cst.Parameters(
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"), default=cst.SimpleString('"one"')
                            ),
                            cst.Param(cst.Name("baz")),
                        )
                    ),
                    cst.Integer("5"),
                ),
                'lambda *, bar = "one", baz: 5',
            ),
            # Mixed params and kwonly_params
            (
                cst.Lambda(
                    cst.Parameters(
                        params=(
                            cst.Param(cst.Name("first")),
                            cst.Param(cst.Name("second")),
                        ),
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"), default=cst.SimpleString('"one"')
                            ),
                            cst.Param(cst.Name("baz")),
                            cst.Param(
                                cst.Name("biz"), default=cst.SimpleString('"two"')
                            ),
                        ),
                    ),
                    cst.Integer("5"),
                ),
                'lambda first, second, *, bar = "one", baz, biz = "two": 5',
            ),
            # Mixed params and kwonly_params
            (
                cst.Lambda(
                    cst.Parameters(
                        params=(
                            cst.Param(cst.Name("first"), default=cst.Float("1.0")),
                            cst.Param(cst.Name("second"), default=cst.Float("1.5")),
                        ),
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"), default=cst.SimpleString('"one"')
                            ),
                            cst.Param(cst.Name("baz")),
                            cst.Param(
                                cst.Name("biz"), default=cst.SimpleString('"two"')
                            ),
                        ),
                    ),
                    cst.Integer("5"),
                ),
                'lambda first = 1.0, second = 1.5, *, bar = "one", baz, biz = "two": 5',
            ),
            # Mixed params and kwonly_params
            (
                cst.Lambda(
                    cst.Parameters(
                        params=(
                            cst.Param(cst.Name("first")),
                            cst.Param(cst.Name("second")),
                            cst.Param(cst.Name("third"), default=cst.Float("1.0")),
                            cst.Param(cst.Name("fourth"), default=cst.Float("1.5")),
                        ),
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"), default=cst.SimpleString('"one"')
                            ),
                            cst.Param(cst.Name("baz")),
                            cst.Param(
                                cst.Name("biz"), default=cst.SimpleString('"two"')
                            ),
                        ),
                    ),
                    cst.Integer("5"),
                ),
                'lambda first, second, third = 1.0, fourth = 1.5, *, bar = "one", baz, biz = "two": 5',
                CodeRange((1, 0), (1, 84)),
            ),
            # Test star_arg
            (
                cst.Lambda(
                    cst.Parameters(star_arg=cst.Param(cst.Name("params"))),
                    cst.Integer("5"),
                ),
                "lambda *params: 5",
            ),
            # Typed star_arg, include kwonly_params
            (
                cst.Lambda(
                    cst.Parameters(
                        star_arg=cst.Param(cst.Name("params")),
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"), default=cst.SimpleString('"one"')
                            ),
                            cst.Param(cst.Name("baz")),
                            cst.Param(
                                cst.Name("biz"), default=cst.SimpleString('"two"')
                            ),
                        ),
                    ),
                    cst.Integer("5"),
                ),
                'lambda *params, bar = "one", baz, biz = "two": 5',
            ),
            # Mixed params, star_arg and kwonly_params
            (
                cst.Lambda(
                    cst.Parameters(
                        params=(
                            cst.Param(cst.Name("first")),
                            cst.Param(cst.Name("second")),
                            cst.Param(cst.Name("third"), default=cst.Float("1.0")),
                            cst.Param(cst.Name("fourth"), default=cst.Float("1.5")),
                        ),
                        star_arg=cst.Param(cst.Name("params")),
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"), default=cst.SimpleString('"one"')
                            ),
                            cst.Param(cst.Name("baz")),
                            cst.Param(
                                cst.Name("biz"), default=cst.SimpleString('"two"')
                            ),
                        ),
                    ),
                    cst.Integer("5"),
                ),
                'lambda first, second, third = 1.0, fourth = 1.5, *params, bar = "one", baz, biz = "two": 5',
            ),
            # Test star_arg and star_kwarg
            (
                cst.Lambda(
                    cst.Parameters(star_kwarg=cst.Param(cst.Name("kwparams"))),
                    cst.Integer("5"),
                ),
                "lambda **kwparams: 5",
            ),
            # Test star_arg and kwarg
            (
                cst.Lambda(
                    cst.Parameters(
                        star_arg=cst.Param(cst.Name("params")),
                        star_kwarg=cst.Param(cst.Name("kwparams")),
                    ),
                    cst.Integer("5"),
                ),
                "lambda *params, **kwparams: 5",
            ),
            # Inner whitespace
            (
                cst.Lambda(
                    lpar=(cst.LeftParen(whitespace_after=cst.SimpleWhitespace(" ")),),
                    whitespace_after_lambda=cst.SimpleWhitespace("  "),
                    params=cst.Parameters(),
                    colon=cst.Colon(whitespace_after=cst.SimpleWhitespace(" ")),
                    body=cst.Integer("5"),
                    rpar=(cst.RightParen(whitespace_before=cst.SimpleWhitespace(" ")),),
                ),
                "( lambda  : 5 )",
                CodeRange((1, 2), (1, 13)),
            ),
        )
    )
    def test_valid(
        self, node: cst.CSTNode, code: str, position: Optional[CodeRange] = None
    ) -> None:
        self.validate_node(node, code, expected_position=position)

    @data_provider(
        (
            (
                lambda: cst.Lambda(
                    cst.Parameters(params=(cst.Param(cst.Name("arg")),)),
                    cst.Integer("5"),
                    lpar=(cst.LeftParen(),),
                ),
                "left paren without right paren",
            ),
            (
                lambda: cst.Lambda(
                    cst.Parameters(params=(cst.Param(cst.Name("arg")),)),
                    cst.Integer("5"),
                    rpar=(cst.RightParen(),),
                ),
                "right paren without left paren",
            ),
            (
                lambda: cst.Lambda(
                    cst.Parameters(posonly_params=(cst.Param(cst.Name("arg")),)),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(""),
                ),
                "at least one space after lambda",
            ),
            (
                lambda: cst.Lambda(
                    cst.Parameters(params=(cst.Param(cst.Name("arg")),)),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(""),
                ),
                "at least one space after lambda",
            ),
            (
                lambda: cst.Lambda(
                    cst.Parameters(
                        params=(cst.Param(cst.Name("arg"), default=cst.Integer("5")),)
                    ),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(""),
                ),
                "at least one space after lambda",
            ),
            (
                lambda: cst.Lambda(
                    cst.Parameters(
                        star_kwarg=cst.Param(cst.Name("bar"), equal=cst.AssignEqual())
                    ),
                    cst.Integer("5"),
                ),
                "Must have a default when specifying an AssignEqual.",
            ),
            (
                lambda: cst.Lambda(
                    cst.Parameters(star_kwarg=cst.Param(cst.Name("bar"), star="***")),
                    cst.Integer("5"),
                ),
                r"Must specify either '', '\*' or '\*\*' for star.",
            ),
            (
                lambda: cst.Lambda(
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("bar"), default=cst.SimpleString('"one"')
                            ),
                            cst.Param(cst.Name("bar")),
                        )
                    ),
                    cst.Integer("5"),
                ),
                "Cannot have param without defaults following a param with defaults.",
            ),
            (
                lambda: cst.Lambda(
                    cst.Parameters(star_arg=cst.ParamStar()), cst.Integer("5")
                ),
                "Must have at least one kwonly param if ParamStar is used.",
            ),
            (
                lambda: cst.Lambda(
                    cst.Parameters(params=(cst.Param(cst.Name("bar"), star="*"),)),
                    cst.Integer("5"),
                ),
                "Expecting a star prefix of ''",
            ),
            (
                lambda: cst.Lambda(
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("bar"),
                                default=cst.SimpleString('"one"'),
                                star="*",
                            ),
                        )
                    ),
                    cst.Integer("5"),
                ),
                "Expecting a star prefix of ''",
            ),
            (
                lambda: cst.Lambda(
                    cst.Parameters(
                        kwonly_params=(cst.Param(cst.Name("bar"), star="*"),)
                    ),
                    cst.Integer("5"),
                ),
                "Expecting a star prefix of ''",
            ),
            (
                lambda: cst.Lambda(
                    cst.Parameters(star_arg=cst.Param(cst.Name("bar"), star="**")),
                    cst.Integer("5"),
                ),
                r"Expecting a star prefix of '\*'",
            ),
            (
                lambda: cst.Lambda(
                    cst.Parameters(star_kwarg=cst.Param(cst.Name("bar"), star="*")),
                    cst.Integer("5"),
                ),
                r"Expecting a star prefix of '\*\*'",
            ),
            (
                lambda: cst.Lambda(
                    cst.Parameters(
                        posonly_params=(
                            cst.Param(
                                cst.Name("arg"),
                                annotation=cst.Annotation(cst.Name("str")),
                            ),
                        )
                    ),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(""),
                ),
                "Lambda params cannot have type annotations",
            ),
            (
                lambda: cst.Lambda(
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("arg"),
                                annotation=cst.Annotation(cst.Name("str")),
                            ),
                        )
                    ),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(""),
                ),
                "Lambda params cannot have type annotations",
            ),
            (
                lambda: cst.Lambda(
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("arg"),
                                default=cst.Integer("5"),
                                annotation=cst.Annotation(cst.Name("str")),
                            ),
                        )
                    ),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(""),
                ),
                "Lambda params cannot have type annotations",
            ),
            (
                lambda: cst.Lambda(
                    cst.Parameters(
                        star_arg=cst.Param(
                            cst.Name("arg"), annotation=cst.Annotation(cst.Name("str"))
                        )
                    ),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(""),
                ),
                "Lambda params cannot have type annotations",
            ),
            (
                lambda: cst.Lambda(
                    cst.Parameters(
                        kwonly_params=(
                            cst.Param(
                                cst.Name("arg"),
                                annotation=cst.Annotation(cst.Name("str")),
                            ),
                        )
                    ),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(""),
                ),
                "Lambda params cannot have type annotations",
            ),
            (
                lambda: cst.Lambda(
                    cst.Parameters(
                        star_kwarg=cst.Param(
                            cst.Name("arg"), annotation=cst.Annotation(cst.Name("str"))
                        )
                    ),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(""),
                ),
                "Lambda params cannot have type annotations",
            ),
        )
    )
    def test_invalid(
        self, get_node: Callable[[], cst.CSTNode], expected_re: str
    ) -> None:
        self.assert_invalid(get_node, expected_re)


def _parse_expression_force_38(code: str) -> cst.BaseExpression:
    return cst.parse_expression(
        code, config=cst.PartialParserConfig(python_version="3.8")
    )


class LambdaParserTest(CSTNodeTest):
    @data_provider(
        (
            # Simple lambda
            (cst.Lambda(cst.Parameters(), cst.Integer("5")), "lambda: 5"),
            # Test basic positional params
            (
                cst.Lambda(
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("bar"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(cst.Name("baz"), star=""),
                        )
                    ),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(" "),
                ),
                "lambda bar, baz: 5",
            ),
            # Test basic positional default params
            (
                cst.Lambda(
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("bar"),
                                default=cst.SimpleString('"one"'),
                                equal=cst.AssignEqual(),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("baz"),
                                default=cst.Integer("5"),
                                equal=cst.AssignEqual(),
                                star="",
                            ),
                        )
                    ),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(" "),
                ),
                'lambda bar = "one", baz = 5: 5',
            ),
            # Mixed positional and default params.
            (
                cst.Lambda(
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("bar"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("baz"),
                                default=cst.Integer("5"),
                                equal=cst.AssignEqual(),
                                star="",
                            ),
                        )
                    ),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(" "),
                ),
                "lambda bar, baz = 5: 5",
            ),
            # Test kwonly params
            (
                cst.Lambda(
                    cst.Parameters(
                        star_arg=cst.ParamStar(),
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                default=cst.SimpleString('"one"'),
                                equal=cst.AssignEqual(),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(cst.Name("baz"), star=""),
                        ),
                    ),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(" "),
                ),
                'lambda *, bar = "one", baz: 5',
            ),
            # Mixed params and kwonly_params
            (
                cst.Lambda(
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("first"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("second"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                        ),
                        star_arg=cst.ParamStar(),
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                default=cst.SimpleString('"one"'),
                                equal=cst.AssignEqual(),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("baz"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("biz"),
                                default=cst.SimpleString('"two"'),
                                equal=cst.AssignEqual(),
                                star="",
                            ),
                        ),
                    ),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(" "),
                ),
                'lambda first, second, *, bar = "one", baz, biz = "two": 5',
            ),
            # Mixed params and kwonly_params
            (
                cst.Lambda(
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("first"),
                                default=cst.Float("1.0"),
                                equal=cst.AssignEqual(),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("second"),
                                default=cst.Float("1.5"),
                                equal=cst.AssignEqual(),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                        ),
                        star_arg=cst.ParamStar(),
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                default=cst.SimpleString('"one"'),
                                equal=cst.AssignEqual(),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("baz"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("biz"),
                                default=cst.SimpleString('"two"'),
                                equal=cst.AssignEqual(),
                                star="",
                            ),
                        ),
                    ),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(" "),
                ),
                'lambda first = 1.0, second = 1.5, *, bar = "one", baz, biz = "two": 5',
            ),
            # Mixed params and kwonly_params
            (
                cst.Lambda(
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("first"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("second"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("third"),
                                default=cst.Float("1.0"),
                                equal=cst.AssignEqual(),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("fourth"),
                                default=cst.Float("1.5"),
                                equal=cst.AssignEqual(),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                        ),
                        star_arg=cst.ParamStar(),
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                default=cst.SimpleString('"one"'),
                                equal=cst.AssignEqual(),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("baz"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("biz"),
                                default=cst.SimpleString('"two"'),
                                equal=cst.AssignEqual(),
                                star="",
                            ),
                        ),
                    ),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(" "),
                ),
                'lambda first, second, third = 1.0, fourth = 1.5, *, bar = "one", baz, biz = "two": 5',
            ),
            # Test star_arg
            (
                cst.Lambda(
                    cst.Parameters(star_arg=cst.Param(cst.Name("params"), star="*")),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(" "),
                ),
                "lambda *params: 5",
            ),
            # Typed star_arg, include kwonly_params
            (
                cst.Lambda(
                    cst.Parameters(
                        star_arg=cst.Param(
                            cst.Name("params"),
                            star="*",
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                default=cst.SimpleString('"one"'),
                                equal=cst.AssignEqual(),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("baz"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("biz"),
                                default=cst.SimpleString('"two"'),
                                equal=cst.AssignEqual(),
                                star="",
                            ),
                        ),
                    ),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(" "),
                ),
                'lambda *params, bar = "one", baz, biz = "two": 5',
            ),
            # Mixed params, star_arg and kwonly_params
            (
                cst.Lambda(
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("first"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("second"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("third"),
                                default=cst.Float("1.0"),
                                equal=cst.AssignEqual(),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("fourth"),
                                default=cst.Float("1.5"),
                                equal=cst.AssignEqual(),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                        ),
                        star_arg=cst.Param(
                            cst.Name("params"),
                            star="*",
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                default=cst.SimpleString('"one"'),
                                equal=cst.AssignEqual(),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("baz"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("biz"),
                                default=cst.SimpleString('"two"'),
                                equal=cst.AssignEqual(),
                                star="",
                            ),
                        ),
                    ),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(" "),
                ),
                'lambda first, second, third = 1.0, fourth = 1.5, *params, bar = "one", baz, biz = "two": 5',
            ),
            # Test star_arg and star_kwarg
            (
                cst.Lambda(
                    cst.Parameters(
                        star_kwarg=cst.Param(cst.Name("kwparams"), star="**")
                    ),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(" "),
                ),
                "lambda **kwparams: 5",
            ),
            # Test star_arg and kwarg
            (
                cst.Lambda(
                    cst.Parameters(
                        star_arg=cst.Param(
                            cst.Name("params"),
                            star="*",
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        star_kwarg=cst.Param(cst.Name("kwparams"), star="**"),
                    ),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(" "),
                ),
                "lambda *params, **kwparams: 5",
            ),
            # Inner whitespace
            (
                cst.Lambda(
                    lpar=(cst.LeftParen(whitespace_after=cst.SimpleWhitespace(" ")),),
                    params=cst.Parameters(),
                    colon=cst.Colon(
                        whitespace_before=cst.SimpleWhitespace("  "),
                        whitespace_after=cst.SimpleWhitespace(" "),
                    ),
                    body=cst.Integer("5"),
                    rpar=(cst.RightParen(whitespace_before=cst.SimpleWhitespace(" ")),),
                ),
                "( lambda  : 5 )",
            ),
            # No space between lambda and params
            (
                cst.Lambda(
                    cst.Parameters(star_arg=cst.Param(cst.Name("args"), star="*")),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(""),
                ),
                "lambda*args: 5",
            ),
            (
                cst.Lambda(
                    cst.Parameters(star_kwarg=cst.Param(cst.Name("kwargs"), star="**")),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(""),
                ),
                "lambda**kwargs: 5",
            ),
            (
                cst.Lambda(
                    cst.Parameters(
                        star_arg=cst.ParamStar(
                            comma=cst.Comma(
                                cst.SimpleWhitespace(""), cst.SimpleWhitespace("")
                            )
                        ),
                        kwonly_params=[cst.Param(cst.Name("args"), star="")],
                    ),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(""),
                ),
                "lambda*,args: 5",
            ),
            (
                cst.ListComp(
                    elt=cst.Lambda(
                        params=cst.Parameters(),
                        body=cst.Tuple(()),
                        colon=cst.Colon(),
                    ),
                    for_in=cst.CompFor(
                        target=cst.Name("_"),
                        iter=cst.Name("_"),
                        whitespace_before=cst.SimpleWhitespace(""),
                    ),
                ),
                "[lambda:()for _ in _]",
            ),
        )
    )
    def test_valid(
        self, node: cst.CSTNode, code: str, position: Optional[CodeRange] = None
    ) -> None:
        self.validate_node(node, code, parse_expression, position)

    @data_provider(
        (
            # Test basic positional only params
            {
                "node": cst.Lambda(
                    cst.Parameters(
                        posonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("baz"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                        ),
                        posonly_ind=cst.ParamSlash(),
                    ),
                    cst.Integer("5"),
                    whitespace_after_lambda=cst.SimpleWhitespace(" "),
                ),
                "code": "lambda bar, baz, /: 5",
            },
        )
    )
    def test_valid_38(
        self, node: cst.CSTNode, code: str, position: Optional[CodeRange] = None
    ) -> None:
        self.validate_node(node, code, _parse_expression_force_38, position)
