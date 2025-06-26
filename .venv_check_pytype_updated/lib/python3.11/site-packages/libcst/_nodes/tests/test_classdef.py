# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any, Callable

import libcst as cst
from libcst import parse_statement
from libcst._nodes.tests.base import CSTNodeTest
from libcst._parser.entrypoints import is_native
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider


class ClassDefCreationTest(CSTNodeTest):
    @data_provider(
        (
            # Simple classdef
            {
                "node": cst.ClassDef(
                    cst.Name("Foo"), cst.SimpleStatementSuite((cst.Pass(),))
                ),
                "code": "class Foo: pass\n",
                "expected_position": CodeRange((1, 0), (1, 15)),
            },
            {
                "node": cst.ClassDef(
                    cst.Name("Foo"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    lpar=cst.LeftParen(),
                    rpar=cst.RightParen(),
                ),
                "code": "class Foo(): pass\n",
            },
            # Positional arguments render test
            {
                "node": cst.ClassDef(
                    cst.Name("Foo"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    bases=(cst.Arg(cst.Name("obj")),),
                ),
                "code": "class Foo(obj): pass\n",
            },
            {
                "node": cst.ClassDef(
                    cst.Name("Foo"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    bases=(
                        cst.Arg(cst.Name("Bar")),
                        cst.Arg(cst.Name("Baz")),
                        cst.Arg(cst.Name("object")),
                    ),
                ),
                "code": "class Foo(Bar, Baz, object): pass\n",
            },
            # Keyword arguments render test
            {
                "node": cst.ClassDef(
                    cst.Name("Foo"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    keywords=(
                        cst.Arg(keyword=cst.Name("metaclass"), value=cst.Name("Bar")),
                    ),
                ),
                "code": "class Foo(metaclass = Bar): pass\n",
                "expected_position": CodeRange((1, 0), (1, 32)),
            },
            # Iterator expansion render test
            {
                "node": cst.ClassDef(
                    cst.Name("Foo"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    bases=(cst.Arg(star="*", value=cst.Name("one")),),
                ),
                "code": "class Foo(*one): pass\n",
            },
            {
                "node": cst.ClassDef(
                    cst.Name("Foo"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    bases=(
                        cst.Arg(star="*", value=cst.Name("one")),
                        cst.Arg(star="*", value=cst.Name("two")),
                        cst.Arg(star="*", value=cst.Name("three")),
                    ),
                ),
                "code": "class Foo(*one, *two, *three): pass\n",
            },
            # Dictionary expansion render test
            {
                "node": cst.ClassDef(
                    cst.Name("Foo"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    keywords=(cst.Arg(star="**", value=cst.Name("one")),),
                ),
                "code": "class Foo(**one): pass\n",
            },
            {
                "node": cst.ClassDef(
                    cst.Name("Foo"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    keywords=(
                        cst.Arg(star="**", value=cst.Name("one")),
                        cst.Arg(star="**", value=cst.Name("two")),
                        cst.Arg(star="**", value=cst.Name("three")),
                    ),
                ),
                "code": "class Foo(**one, **two, **three): pass\n",
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            {
                "node": cst.ClassDef(
                    cst.Name("Foo"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    type_parameters=cst.TypeParameters(
                        (
                            cst.TypeParam(
                                cst.TypeVar(
                                    cst.Name("T"),
                                    bound=cst.Name("int"),
                                    colon=cst.Colon(
                                        whitespace_after=cst.SimpleWhitespace(" ")
                                    ),
                                ),
                                cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                            ),
                            cst.TypeParam(
                                cst.TypeVarTuple(cst.Name("Ts")),
                                cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                            ),
                            cst.TypeParam(cst.ParamSpec(cst.Name("KW"))),
                        )
                    ),
                ),
                "code": "class Foo[T: int, *Ts, **KW]: pass\n",
            },
            {
                "node": cst.ClassDef(
                    cst.Name("Foo"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    type_parameters=cst.TypeParameters(
                        params=(
                            cst.TypeParam(
                                param=cst.TypeVar(
                                    cst.Name("T"),
                                    bound=cst.Name("str"),
                                    colon=cst.Colon(
                                        whitespace_before=cst.SimpleWhitespace(" "),
                                        whitespace_after=cst.ParenthesizedWhitespace(
                                            empty_lines=(cst.EmptyLine(),),
                                            indent=True,
                                        ),
                                    ),
                                ),
                                comma=cst.Comma(cst.SimpleWhitespace(" ")),
                            ),
                            cst.TypeParam(
                                cst.ParamSpec(
                                    cst.Name("PS"), cst.SimpleWhitespace("  ")
                                ),
                                cst.Comma(cst.SimpleWhitespace("  ")),
                            ),
                        )
                    ),
                    whitespace_after_type_parameters=cst.SimpleWhitespace("  "),
                ),
                "code": "class Foo[T :\n\nstr ,**  PS  ,]  : pass\n",
            },
            {
                "node": cst.ClassDef(
                    cst.Name("Foo"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    type_parameters=cst.TypeParameters(
                        params=(
                            cst.TypeParam(
                                param=cst.TypeVar(
                                    cst.Name("T"),
                                    bound=cst.Name("str"),
                                    colon=cst.Colon(
                                        whitespace_before=cst.SimpleWhitespace(" "),
                                        whitespace_after=cst.ParenthesizedWhitespace(
                                            empty_lines=(cst.EmptyLine(),),
                                            indent=True,
                                        ),
                                    ),
                                ),
                                comma=cst.Comma(cst.SimpleWhitespace(" ")),
                            ),
                            cst.TypeParam(
                                cst.ParamSpec(
                                    cst.Name("PS"), cst.SimpleWhitespace("  ")
                                ),
                                cst.Comma(cst.SimpleWhitespace("  ")),
                            ),
                        )
                    ),
                    lpar=cst.LeftParen(),
                    rpar=cst.RightParen(),
                    whitespace_after_type_parameters=cst.SimpleWhitespace("  "),
                ),
                "code": "class Foo[T :\n\nstr ,**  PS  ,]  (): pass\n",
            },
        )
    )
    def test_valid_native(self, **kwargs: Any) -> None:
        if not is_native():
            self.skipTest("Disabled for pure python parser")
        self.validate_node(**kwargs)

    @data_provider(
        (
            # Basic parenthesis tests.
            (
                lambda: cst.ClassDef(
                    name=cst.Name("Foo"),
                    body=cst.SimpleStatementSuite((cst.Pass(),)),
                    lpar=cst.LeftParen(),
                ),
                "Do not mix concrete LeftParen/RightParen with MaybeSentinel",
            ),
            (
                lambda: cst.ClassDef(
                    name=cst.Name("Foo"),
                    body=cst.SimpleStatementSuite((cst.Pass(),)),
                    rpar=cst.RightParen(),
                ),
                "Do not mix concrete LeftParen/RightParen with MaybeSentinel",
            ),
            # Whitespace validation
            (
                lambda: cst.ClassDef(
                    name=cst.Name("Foo"),
                    body=cst.SimpleStatementSuite((cst.Pass(),)),
                    whitespace_after_class=cst.SimpleWhitespace(""),
                ),
                "at least one space between 'class' and name",
            ),
        )
    )
    def test_invalid(
        self, get_node: Callable[[], cst.CSTNode], expected_re: str
    ) -> None:
        self.assert_invalid(get_node, expected_re)


class ClassDefParserTest(CSTNodeTest):
    @data_provider(
        (
            # Simple classdef
            {
                "node": cst.ClassDef(
                    cst.Name("Foo"), cst.SimpleStatementSuite((cst.Pass(),))
                ),
                "code": "class Foo: pass\n",
            },
            {
                "node": cst.ClassDef(
                    cst.Name("Foo"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    lpar=cst.LeftParen(),
                    rpar=cst.RightParen(),
                ),
                "code": "class Foo(): pass\n",
            },
            # Positional arguments render test
            {
                "node": cst.ClassDef(
                    cst.Name("Foo"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    lpar=cst.LeftParen(),
                    bases=(cst.Arg(cst.Name("obj")),),
                    rpar=cst.RightParen(),
                ),
                "code": "class Foo(obj): pass\n",
            },
            {
                "node": cst.ClassDef(
                    cst.Name("Foo"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    lpar=cst.LeftParen(),
                    bases=(
                        cst.Arg(
                            cst.Name("Bar"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            cst.Name("Baz"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(cst.Name("object")),
                    ),
                    rpar=cst.RightParen(),
                ),
                "code": "class Foo(Bar, Baz, object): pass\n",
            },
            # Keyword arguments render test
            {
                "node": cst.ClassDef(
                    cst.Name("Foo"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    lpar=cst.LeftParen(),
                    keywords=(
                        cst.Arg(
                            keyword=cst.Name("metaclass"),
                            equal=cst.AssignEqual(),
                            value=cst.Name("Bar"),
                        ),
                    ),
                    rpar=cst.RightParen(),
                ),
                "code": "class Foo(metaclass = Bar): pass\n",
            },
            # Iterator expansion render test
            {
                "node": cst.ClassDef(
                    cst.Name("Foo"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    lpar=cst.LeftParen(),
                    bases=(cst.Arg(star="*", value=cst.Name("one")),),
                    rpar=cst.RightParen(),
                ),
                "code": "class Foo(*one): pass\n",
            },
            {
                "node": cst.ClassDef(
                    cst.Name("Foo"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    lpar=cst.LeftParen(),
                    bases=(
                        cst.Arg(
                            star="*",
                            value=cst.Name("one"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            star="*",
                            value=cst.Name("two"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(star="*", value=cst.Name("three")),
                    ),
                    rpar=cst.RightParen(),
                ),
                "code": "class Foo(*one, *two, *three): pass\n",
            },
            # Dictionary expansion render test
            {
                "node": cst.ClassDef(
                    cst.Name("Foo"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    lpar=cst.LeftParen(),
                    keywords=(cst.Arg(star="**", value=cst.Name("one")),),
                    rpar=cst.RightParen(),
                ),
                "code": "class Foo(**one): pass\n",
            },
            {
                "node": cst.ClassDef(
                    cst.Name("Foo"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    lpar=cst.LeftParen(),
                    keywords=(
                        cst.Arg(
                            star="**",
                            value=cst.Name("one"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(
                            star="**",
                            value=cst.Name("two"),
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        cst.Arg(star="**", value=cst.Name("three")),
                    ),
                    rpar=cst.RightParen(),
                ),
                "code": "class Foo(**one, **two, **three): pass\n",
            },
            # Decorator render tests
            {
                "node": cst.ClassDef(
                    cst.Name("Foo"),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    decorators=(cst.Decorator(cst.Name("foo")),),
                    lpar=cst.LeftParen(),
                    rpar=cst.RightParen(),
                ),
                "code": "@foo\nclass Foo(): pass\n",
                "expected_position": CodeRange((2, 0), (2, 17)),
            },
            {
                "node": cst.ClassDef(
                    leading_lines=(
                        cst.EmptyLine(),
                        cst.EmptyLine(comment=cst.Comment("# leading comment 1")),
                    ),
                    decorators=(
                        cst.Decorator(cst.Name("foo"), leading_lines=()),
                        cst.Decorator(
                            cst.Name("bar"),
                            leading_lines=(
                                cst.EmptyLine(
                                    comment=cst.Comment("# leading comment 2")
                                ),
                            ),
                        ),
                        cst.Decorator(
                            cst.Name("baz"),
                            leading_lines=(
                                cst.EmptyLine(
                                    comment=cst.Comment("# leading comment 3")
                                ),
                            ),
                        ),
                    ),
                    lines_after_decorators=(
                        cst.EmptyLine(comment=cst.Comment("# class comment")),
                    ),
                    name=cst.Name("Foo"),
                    body=cst.SimpleStatementSuite((cst.Pass(),)),
                    lpar=cst.LeftParen(),
                    rpar=cst.RightParen(),
                ),
                "code": "\n# leading comment 1\n@foo\n# leading comment 2\n@bar\n# leading comment 3\n@baz\n# class comment\nclass Foo(): pass\n",
                "expected_position": CodeRange((9, 0), (9, 17)),
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs, parser=parse_statement)
