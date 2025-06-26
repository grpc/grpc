# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any

import libcst as cst
from libcst import parse_statement
from libcst._nodes.tests.base import CSTNodeTest
from libcst._parser.entrypoints import is_native
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider


class TypeAliasCreationTest(CSTNodeTest):
    @data_provider(
        (
            {
                "node": cst.TypeAlias(
                    cst.Name("foo"),
                    cst.Name("bar"),
                ),
                "code": "type foo = bar",
                "expected_position": CodeRange((1, 0), (1, 14)),
            },
            {
                "node": cst.TypeAlias(
                    cst.Name("foo"),
                    type_parameters=cst.TypeParameters(
                        [cst.TypeParam(cst.TypeVar(cst.Name("T")))]
                    ),
                    value=cst.BinaryOperation(
                        cst.Name("bar"), cst.BitOr(), cst.Name("baz")
                    ),
                ),
                "code": "type foo[T] = bar | baz",
                "expected_position": CodeRange((1, 0), (1, 23)),
            },
            {
                "node": cst.TypeAlias(
                    cst.Name("foo"),
                    type_parameters=cst.TypeParameters(
                        [
                            cst.TypeParam(
                                cst.TypeVar(cst.Name("T"), bound=cst.Name("str"))
                            ),
                            cst.TypeParam(cst.TypeVarTuple(cst.Name("Ts"))),
                            cst.TypeParam(cst.ParamSpec(cst.Name("KW"))),
                        ]
                    ),
                    value=cst.BinaryOperation(
                        cst.Name("bar"), cst.BitOr(), cst.Name("baz")
                    ),
                ),
                "code": "type foo[T: str, *Ts, **KW] = bar | baz",
                "expected_position": CodeRange((1, 0), (1, 39)),
            },
            {
                "node": cst.TypeAlias(
                    cst.Name("foo"),
                    type_parameters=cst.TypeParameters(
                        [
                            cst.TypeParam(
                                cst.TypeVar(cst.Name("T")), default=cst.Name("str")
                            ),
                        ]
                    ),
                    value=cst.Name("bar"),
                ),
                "code": "type foo[T = str] = bar",
                "expected_position": CodeRange((1, 0), (1, 23)),
            },
            {
                "node": cst.TypeAlias(
                    cst.Name("foo"),
                    type_parameters=cst.TypeParameters(
                        [
                            cst.TypeParam(
                                cst.ParamSpec(cst.Name("P")),
                                default=cst.List(
                                    elements=[
                                        cst.Element(cst.Name("int")),
                                        cst.Element(cst.Name("str")),
                                    ]
                                ),
                            ),
                        ]
                    ),
                    value=cst.Name("bar"),
                ),
                "code": "type foo[**P = [int, str]] = bar",
                "expected_position": CodeRange((1, 0), (1, 32)),
            },
            {
                "node": cst.TypeAlias(
                    cst.Name("foo"),
                    type_parameters=cst.TypeParameters(
                        [
                            cst.TypeParam(
                                cst.TypeVarTuple(cst.Name("T")),
                                equal=cst.AssignEqual(),
                                default=cst.Name("default"),
                                star="*",
                            ),
                        ]
                    ),
                    value=cst.Name("bar"),
                ),
                "code": "type foo[*T = *default] = bar",
                "expected_position": CodeRange((1, 0), (1, 29)),
            },
            {
                "node": cst.TypeAlias(
                    cst.Name("foo"),
                    type_parameters=cst.TypeParameters(
                        [
                            cst.TypeParam(
                                cst.TypeVarTuple(cst.Name("T")),
                                equal=cst.AssignEqual(),
                                default=cst.Name("default"),
                                star="*",
                                whitespace_after_star=cst.SimpleWhitespace("  "),
                            ),
                        ]
                    ),
                    value=cst.Name("bar"),
                ),
                "code": "type foo[*T = *  default] = bar",
                "expected_position": CodeRange((1, 0), (1, 31)),
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        if not is_native():
            self.skipTest("Disabled in the old parser")
        self.validate_node(**kwargs)


class TypeAliasParserTest(CSTNodeTest):
    @data_provider(
        (
            {
                "node": cst.SimpleStatementLine(
                    [
                        cst.TypeAlias(
                            cst.Name("foo"),
                            cst.Name("bar"),
                            whitespace_after_name=cst.SimpleWhitespace(" "),
                        )
                    ]
                ),
                "code": "type foo = bar\n",
                "parser": parse_statement,
            },
            {
                "node": cst.SimpleStatementLine(
                    [
                        cst.TypeAlias(
                            cst.Name("foo"),
                            cst.Name("bar"),
                            type_parameters=cst.TypeParameters(
                                params=[
                                    cst.TypeParam(
                                        cst.TypeVar(
                                            cst.Name("T"), cst.Name("str"), cst.Colon()
                                        ),
                                        cst.Comma(),
                                    ),
                                    cst.TypeParam(
                                        cst.ParamSpec(
                                            cst.Name("KW"),
                                            whitespace_after_star=cst.SimpleWhitespace(
                                                "  "
                                            ),
                                        ),
                                        cst.Comma(
                                            whitespace_before=cst.SimpleWhitespace(" "),
                                            whitespace_after=cst.SimpleWhitespace(" "),
                                        ),
                                    ),
                                ],
                                rbracket=cst.RightSquareBracket(
                                    cst.SimpleWhitespace("")
                                ),
                            ),
                            whitespace_after_name=cst.SimpleWhitespace(" "),
                            whitespace_after_type=cst.SimpleWhitespace("  "),
                            whitespace_after_equals=cst.SimpleWhitespace("  "),
                            whitespace_after_type_parameters=cst.SimpleWhitespace("  "),
                            semicolon=cst.Semicolon(
                                whitespace_before=cst.SimpleWhitespace(" "),
                                whitespace_after=cst.SimpleWhitespace(" "),
                            ),
                        )
                    ]
                ),
                "code": "type  foo [T:str,**  KW , ]  =  bar ; \n",
                "parser": parse_statement,
            },
            {
                "node": cst.SimpleStatementLine(
                    [
                        cst.TypeAlias(
                            cst.Name("foo"),
                            type_parameters=cst.TypeParameters(
                                [
                                    cst.TypeParam(
                                        cst.TypeVarTuple(cst.Name("P")),
                                        star="*",
                                        equal=cst.AssignEqual(),
                                        default=cst.Name("default"),
                                    ),
                                ]
                            ),
                            value=cst.Name("bar"),
                            whitespace_after_name=cst.SimpleWhitespace(" "),
                            whitespace_after_type_parameters=cst.SimpleWhitespace(" "),
                        )
                    ]
                ),
                "code": "type foo [*P = *default] = bar\n",
                "parser": parse_statement,
            },
            {
                "node": cst.SimpleStatementLine(
                    [
                        cst.TypeAlias(
                            cst.Name("foo"),
                            type_parameters=cst.TypeParameters(
                                [
                                    cst.TypeParam(
                                        cst.TypeVarTuple(cst.Name("P")),
                                        star="*",
                                        whitespace_after_star=cst.SimpleWhitespace(
                                            "   "
                                        ),
                                        equal=cst.AssignEqual(),
                                        default=cst.Name("default"),
                                    ),
                                ]
                            ),
                            value=cst.Name("bar"),
                            whitespace_after_name=cst.SimpleWhitespace(" "),
                            whitespace_after_type_parameters=cst.SimpleWhitespace(" "),
                        )
                    ]
                ),
                "code": "type foo [*P = *   default] = bar\n",
                "parser": parse_statement,
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        if not is_native():
            self.skipTest("Disabled in the old parser")
        self.validate_node(**kwargs)
