# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any, Callable

import libcst as cst
from libcst import parse_statement
from libcst._nodes.tests.base import CSTNodeTest, DummyIndentedBlock, parse_statement_as
from libcst._parser.entrypoints import is_native
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider


class FunctionDefCreationTest(CSTNodeTest):
    @data_provider(
        (
            # Simple function definition without any arguments or return
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "def foo(): pass\n",
            },
            # Functiondef with a return annotation
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    returns=cst.Annotation(cst.Name("str")),
                ),
                "code": "def foo() -> str: pass\n",
                "expected_position": CodeRange((1, 0), (1, 22)),
            },
            # Async function definition.
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    asynchronous=cst.Asynchronous(),
                ),
                "code": "async def foo(): pass\n",
            },
            # Async function definition with annotation.
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    asynchronous=cst.Asynchronous(),
                    returns=cst.Annotation(cst.Name("int")),
                ),
                "code": "async def foo() -> int: pass\n",
                "expected_position": CodeRange((1, 0), (1, 28)),
            },
            # Test basic positional params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        params=(cst.Param(cst.Name("bar")), cst.Param(cst.Name("baz")))
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "def foo(bar, baz): pass\n",
            },
            # Typed positional params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        params=(
                            cst.Param(cst.Name("bar"), cst.Annotation(cst.Name("str"))),
                            cst.Param(cst.Name("baz"), cst.Annotation(cst.Name("int"))),
                        )
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "def foo(bar: str, baz: int): pass\n",
            },
            # Test basic positional default params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("bar"), default=cst.SimpleString('"one"')
                            ),
                            cst.Param(cst.Name("baz"), default=cst.Integer("5")),
                        )
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": 'def foo(bar = "one", baz = 5): pass\n',
            },
            # Typed positional default params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("bar"),
                                cst.Annotation(cst.Name("str")),
                                default=cst.SimpleString('"one"'),
                            ),
                            cst.Param(
                                cst.Name("baz"),
                                cst.Annotation(cst.Name("int")),
                                default=cst.Integer("5"),
                            ),
                        )
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": 'def foo(bar: str = "one", baz: int = 5): pass\n',
            },
            # Test basic positional only params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        posonly_params=(
                            cst.Param(cst.Name("bar")),
                            cst.Param(cst.Name("baz")),
                        )
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "def foo(bar, baz, /): pass\n",
            },
            # Typed positional only params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        posonly_params=(
                            cst.Param(cst.Name("bar"), cst.Annotation(cst.Name("str"))),
                            cst.Param(cst.Name("baz"), cst.Annotation(cst.Name("int"))),
                        )
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "def foo(bar: str, baz: int, /): pass\n",
            },
            # Test basic positional only default params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        posonly_params=(
                            cst.Param(
                                cst.Name("bar"), default=cst.SimpleString('"one"')
                            ),
                            cst.Param(cst.Name("baz"), default=cst.Integer("5")),
                        )
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": 'def foo(bar = "one", baz = 5, /): pass\n',
            },
            # Typed positional only default params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        posonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                cst.Annotation(cst.Name("str")),
                                default=cst.SimpleString('"one"'),
                            ),
                            cst.Param(
                                cst.Name("baz"),
                                cst.Annotation(cst.Name("int")),
                                default=cst.Integer("5"),
                            ),
                        )
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": 'def foo(bar: str = "one", baz: int = 5, /): pass\n',
            },
            # Mixed positional and default params.
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        params=(
                            cst.Param(cst.Name("bar"), cst.Annotation(cst.Name("str"))),
                            cst.Param(
                                cst.Name("baz"),
                                cst.Annotation(cst.Name("int")),
                                default=cst.Integer("5"),
                            ),
                        )
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "def foo(bar: str, baz: int = 5): pass\n",
            },
            # Test kwonly params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"), default=cst.SimpleString('"one"')
                            ),
                            cst.Param(cst.Name("baz")),
                        )
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": 'def foo(*, bar = "one", baz): pass\n',
            },
            # Typed kwonly params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                cst.Annotation(cst.Name("str")),
                                default=cst.SimpleString('"one"'),
                            ),
                            cst.Param(cst.Name("baz"), cst.Annotation(cst.Name("int"))),
                            cst.Param(
                                cst.Name("biz"),
                                cst.Annotation(cst.Name("str")),
                                default=cst.SimpleString('"two"'),
                            ),
                        )
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": 'def foo(*, bar: str = "one", baz: int, biz: str = "two"): pass\n',
            },
            # Mixed params and kwonly_params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        params=(
                            cst.Param(cst.Name("first")),
                            cst.Param(cst.Name("second")),
                        ),
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                cst.Annotation(cst.Name("str")),
                                default=cst.SimpleString('"one"'),
                            ),
                            cst.Param(cst.Name("baz"), cst.Annotation(cst.Name("int"))),
                            cst.Param(
                                cst.Name("biz"),
                                cst.Annotation(cst.Name("str")),
                                default=cst.SimpleString('"two"'),
                            ),
                        ),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": 'def foo(first, second, *, bar: str = "one", baz: int, biz: str = "two"): pass\n',
            },
            # Mixed params and kwonly_params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        params=(
                            cst.Param(cst.Name("first"), default=cst.Float("1.0")),
                            cst.Param(cst.Name("second"), default=cst.Float("1.5")),
                        ),
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                cst.Annotation(cst.Name("str")),
                                default=cst.SimpleString('"one"'),
                            ),
                            cst.Param(cst.Name("baz"), cst.Annotation(cst.Name("int"))),
                            cst.Param(
                                cst.Name("biz"),
                                cst.Annotation(cst.Name("str")),
                                default=cst.SimpleString('"two"'),
                            ),
                        ),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": 'def foo(first = 1.0, second = 1.5, *, bar: str = "one", baz: int, biz: str = "two"): pass\n',
            },
            # Mixed params and kwonly_params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        params=(
                            cst.Param(cst.Name("first")),
                            cst.Param(cst.Name("second")),
                            cst.Param(cst.Name("third"), default=cst.Float("1.0")),
                            cst.Param(cst.Name("fourth"), default=cst.Float("1.5")),
                        ),
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                cst.Annotation(cst.Name("str")),
                                default=cst.SimpleString('"one"'),
                            ),
                            cst.Param(cst.Name("baz"), cst.Annotation(cst.Name("int"))),
                            cst.Param(
                                cst.Name("biz"),
                                cst.Annotation(cst.Name("str")),
                                default=cst.SimpleString('"two"'),
                            ),
                        ),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": 'def foo(first, second, third = 1.0, fourth = 1.5, *, bar: str = "one", baz: int, biz: str = "two"): pass\n',
            },
            # Test star_arg
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(star_arg=cst.Param(cst.Name("params"))),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "def foo(*params): pass\n",
            },
            # Typed star_arg, include kwonly_params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        star_arg=cst.Param(
                            cst.Name("params"), cst.Annotation(cst.Name("str"))
                        ),
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                cst.Annotation(cst.Name("str")),
                                default=cst.SimpleString('"one"'),
                            ),
                            cst.Param(cst.Name("baz"), cst.Annotation(cst.Name("int"))),
                            cst.Param(
                                cst.Name("biz"),
                                cst.Annotation(cst.Name("str")),
                                default=cst.SimpleString('"two"'),
                            ),
                        ),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": 'def foo(*params: str, bar: str = "one", baz: int, biz: str = "two"): pass\n',
            },
            # Mixed params star_arg and kwonly_params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        params=(
                            cst.Param(cst.Name("first")),
                            cst.Param(cst.Name("second")),
                            cst.Param(cst.Name("third"), default=cst.Float("1.0")),
                            cst.Param(cst.Name("fourth"), default=cst.Float("1.5")),
                        ),
                        star_arg=cst.Param(
                            cst.Name("params"), cst.Annotation(cst.Name("str"))
                        ),
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                cst.Annotation(cst.Name("str")),
                                default=cst.SimpleString('"one"'),
                            ),
                            cst.Param(cst.Name("baz"), cst.Annotation(cst.Name("int"))),
                            cst.Param(
                                cst.Name("biz"),
                                cst.Annotation(cst.Name("str")),
                                default=cst.SimpleString('"two"'),
                            ),
                        ),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": 'def foo(first, second, third = 1.0, fourth = 1.5, *params: str, bar: str = "one", baz: int, biz: str = "two"): pass\n',
            },
            # Test star_arg and star_kwarg
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(star_kwarg=cst.Param(cst.Name("kwparams"))),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "def foo(**kwparams): pass\n",
            },
            # Test star_arg and kwarg
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        star_arg=cst.Param(cst.Name("params")),
                        star_kwarg=cst.Param(cst.Name("kwparams")),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "def foo(*params, **kwparams): pass\n",
            },
            # Test typed star_arg and star_kwarg
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        star_arg=cst.Param(
                            cst.Name("params"), cst.Annotation(cst.Name("str"))
                        ),
                        star_kwarg=cst.Param(
                            cst.Name("kwparams"), cst.Annotation(cst.Name("int"))
                        ),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "def foo(*params: str, **kwparams: int): pass\n",
            },
            # Test positional only params and positional params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        posonly_params=(cst.Param(cst.Name("bar")),),
                        params=(cst.Param(cst.Name("baz")),),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "def foo(bar, /, baz): pass\n",
            },
            # Test positional only params and star args
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        posonly_params=(cst.Param(cst.Name("bar")),),
                        star_arg=cst.Param(cst.Name("baz")),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "def foo(bar, /, *baz): pass\n",
            },
            # Test positional only params and kwonly params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        posonly_params=(cst.Param(cst.Name("bar")),),
                        kwonly_params=(cst.Param(cst.Name("baz")),),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "def foo(bar, /, *, baz): pass\n",
            },
            # Test positional only params and star kwargs
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        posonly_params=(cst.Param(cst.Name("bar")),),
                        star_kwarg=cst.Param(cst.Name("baz")),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "def foo(bar, /, **baz): pass\n",
            },
            # Test decorators
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    (cst.Decorator(cst.Name("bar")),),
                ),
                "code": "@bar\ndef foo(): pass\n",
            },
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    (
                        cst.Decorator(
                            cst.Call(
                                cst.Name("bar"),
                                (
                                    cst.Arg(cst.Name("baz")),
                                    cst.Arg(cst.SimpleString("'123'")),
                                ),
                            )
                        ),
                    ),
                ),
                "code": "@bar(baz, '123')\ndef foo(): pass\n",
            },
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    (
                        cst.Decorator(
                            cst.Call(
                                cst.Name("bar"), (cst.Arg(cst.SimpleString("'123'")),)
                            )
                        ),
                        cst.Decorator(
                            cst.Call(
                                cst.Name("baz"), (cst.Arg(cst.SimpleString("'456'")),)
                            )
                        ),
                    ),
                ),
                "code": "@bar('123')\n@baz('456')\ndef foo(): pass\n",
                "expected_position": CodeRange((3, 0), (3, 15)),
            },
            # Test indentation
            {
                "node": DummyIndentedBlock(
                    "    ",
                    cst.FunctionDef(
                        cst.Name("foo"),
                        cst.Parameters(),
                        cst.SimpleStatementSuite((cst.Pass(),)),
                        (cst.Decorator(cst.Name("bar")),),
                    ),
                ),
                "code": "    @bar\n    def foo(): pass\n",
            },
            # With an indented body
            {
                "node": DummyIndentedBlock(
                    "    ",
                    cst.FunctionDef(
                        cst.Name("foo"),
                        cst.Parameters(),
                        cst.IndentedBlock((cst.SimpleStatementLine((cst.Pass(),)),)),
                        (cst.Decorator(cst.Name("bar")),),
                    ),
                ),
                "code": "    @bar\n    def foo():\n        pass\n",
                "expected_position": CodeRange((2, 4), (3, 12)),
            },
            # Leading lines
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    leading_lines=(
                        cst.EmptyLine(comment=cst.Comment("# leading comment")),
                    ),
                ),
                "code": "# leading comment\ndef foo(): pass\n",
                "expected_position": CodeRange((2, 0), (2, 15)),
            },
            # Inner whitespace
            {
                "node": cst.FunctionDef(
                    leading_lines=(
                        cst.EmptyLine(),
                        cst.EmptyLine(
                            comment=cst.Comment("# What an amazing decorator")
                        ),
                    ),
                    decorators=(
                        cst.Decorator(
                            whitespace_after_at=cst.SimpleWhitespace(" "),
                            decorator=cst.Call(
                                func=cst.Name("bar"),
                                whitespace_after_func=cst.SimpleWhitespace(" "),
                                whitespace_before_args=cst.SimpleWhitespace("  "),
                            ),
                        ),
                    ),
                    lines_after_decorators=(
                        cst.EmptyLine(comment=cst.Comment("# What a great function")),
                    ),
                    asynchronous=cst.Asynchronous(
                        whitespace_after=cst.SimpleWhitespace("  ")
                    ),
                    whitespace_after_def=cst.SimpleWhitespace("  "),
                    name=cst.Name("foo"),
                    whitespace_after_name=cst.SimpleWhitespace("  "),
                    whitespace_before_params=cst.SimpleWhitespace("  "),
                    params=cst.Parameters(),
                    returns=cst.Annotation(
                        whitespace_before_indicator=cst.SimpleWhitespace("  "),
                        whitespace_after_indicator=cst.SimpleWhitespace("  "),
                        annotation=cst.Name("str"),
                    ),
                    whitespace_before_colon=cst.SimpleWhitespace(" "),
                    body=cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "\n# What an amazing decorator\n@ bar (  )\n# What a great function\nasync  def  foo  (  )  ->  str : pass\n",
                "expected_position": CodeRange((5, 0), (5, 37)),
            },
            # Decorators and annotations
            {
                "node": cst.Decorator(
                    whitespace_after_at=cst.SimpleWhitespace(" "),
                    decorator=cst.Call(
                        func=cst.Name("bar"),
                        whitespace_after_func=cst.SimpleWhitespace(" "),
                        whitespace_before_args=cst.SimpleWhitespace("  "),
                    ),
                ),
                "code": "@ bar (  )\n",
                "expected_position": CodeRange((1, 0), (1, 10)),
            },
            # Allow nested calls on decorator
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    (cst.Decorator(cst.Call(func=cst.Call(func=cst.Name("bar")))),),
                ),
                "code": "@bar()()\ndef foo(): pass\n",
            },
            # Allow any expression in decorator
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    (
                        cst.Decorator(
                            cst.BinaryOperation(cst.Name("a"), cst.Add(), cst.Name("b"))
                        ),
                    ),
                ),
                "code": "@a + b\ndef foo(): pass\n",
            },
            # Allow parentheses around decorator
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    (
                        cst.Decorator(
                            cst.Name(
                                "bar", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)
                            )
                        ),
                    ),
                ),
                "code": "@(bar)\ndef foo(): pass\n",
            },
            # Parameters
            {
                "node": cst.Parameters(
                    params=(
                        cst.Param(cst.Name("first")),
                        cst.Param(cst.Name("second")),
                        cst.Param(cst.Name("third"), default=cst.Float("1.0")),
                        cst.Param(cst.Name("fourth"), default=cst.Float("1.5")),
                    ),
                    star_arg=cst.Param(
                        cst.Name("params"), cst.Annotation(cst.Name("str"))
                    ),
                    kwonly_params=(
                        cst.Param(
                            cst.Name("bar"),
                            cst.Annotation(cst.Name("str")),
                            default=cst.SimpleString('"one"'),
                        ),
                        cst.Param(cst.Name("baz"), cst.Annotation(cst.Name("int"))),
                        cst.Param(
                            cst.Name("biz"),
                            cst.Annotation(cst.Name("str")),
                            default=cst.SimpleString('"two"'),
                        ),
                    ),
                ),
                "code": 'first, second, third = 1.0, fourth = 1.5, *params: str, bar: str = "one", baz: int, biz: str = "two"',
                "expected_position": CodeRange((1, 0), (1, 100)),
            },
            {
                "node": cst.Param(cst.Name("third"), star="", default=cst.Float("1.0")),
                "code": "third = 1.0",
                "expected_position": CodeRange((1, 0), (1, 5)),
            },
            {
                "node": cst.Param(
                    cst.Name("third"),
                    star="*",
                    whitespace_after_star=cst.SimpleWhitespace(" "),
                ),
                "code": "* third",
                "expected_position": CodeRange((1, 0), (1, 7)),
            },
            {
                "node": cst.FunctionDef(
                    name=cst.Name(
                        value="foo",
                    ),
                    params=cst.Parameters(
                        params=[
                            cst.Param(
                                name=cst.Name(
                                    value="param1",
                                ),
                            ),
                        ],
                    ),
                    body=cst.IndentedBlock(
                        body=[
                            cst.SimpleStatementLine(
                                body=[
                                    cst.Pass(),
                                ],
                            ),
                        ],
                    ),
                    whitespace_before_params=cst.ParenthesizedWhitespace(
                        last_line=cst.SimpleWhitespace(
                            value="    ",
                        ),
                    ),
                ),
                "code": "def foo(\n    param1):\n    pass\n",
                "expected_position": CodeRange((1, 0), (3, 8)),
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        if not is_native() and kwargs.get("native_only", False):
            self.skipTest("Disabled for native parser")
        if "native_only" in kwargs:
            kwargs.pop("native_only")
        self.validate_node(**kwargs)

    @data_provider(
        (
            # PEP 646
            {
                "node": cst.FunctionDef(
                    name=cst.Name(value="foo"),
                    params=cst.Parameters(
                        params=[],
                        star_arg=cst.Param(
                            star="*",
                            name=cst.Name("a"),
                            annotation=cst.Annotation(
                                cst.StarredElement(value=cst.Name("b")),
                                whitespace_before_indicator=cst.SimpleWhitespace(""),
                            ),
                        ),
                    ),
                    body=cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "parser": parse_statement,
                "code": "def foo(*a: *b): pass\n",
            },
            {
                "node": cst.FunctionDef(
                    name=cst.Name(value="foo"),
                    params=cst.Parameters(
                        params=[],
                        star_arg=cst.Param(
                            star="*",
                            name=cst.Name("a"),
                            annotation=cst.Annotation(
                                cst.StarredElement(
                                    value=cst.Subscript(
                                        value=cst.Name("tuple"),
                                        slice=[
                                            cst.SubscriptElement(
                                                cst.Index(cst.Name("int")),
                                                comma=cst.Comma(),
                                            ),
                                            cst.SubscriptElement(
                                                cst.Index(
                                                    value=cst.Name("Ts"),
                                                    star="*",
                                                    whitespace_after_star=cst.SimpleWhitespace(
                                                        ""
                                                    ),
                                                ),
                                                comma=cst.Comma(),
                                            ),
                                            cst.SubscriptElement(
                                                cst.Index(cst.Ellipsis())
                                            ),
                                        ],
                                    )
                                ),
                                whitespace_before_indicator=cst.SimpleWhitespace(""),
                            ),
                        ),
                    ),
                    body=cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "parser": parse_statement,
                "code": "def foo(*a: *tuple[int,*Ts,...]): pass\n",
            },
            # Single type variable
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    type_parameters=cst.TypeParameters(
                        (cst.TypeParam(cst.TypeVar(cst.Name("T"))),)
                    ),
                ),
                "code": "def foo[T](): pass\n",
                "parser": parse_statement,
            },
            # All the type parameters
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
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
                "code": "def foo[T: int, *Ts, **KW](): pass\n",
                "parser": parse_statement,
            },
            # Type parameters with whitespace
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
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
                "code": "def foo[T :\n\nstr ,**  PS  ,]  (): pass\n",
                "parser": parse_statement,
            },
        )
    )
    def test_valid_native(self, **kwargs: Any) -> None:
        if not is_native():
            self.skipTest("Disabled for pure python parser")
        self.validate_node(**kwargs)

    @data_provider(
        (
            (
                lambda: cst.FunctionDef(
                    cst.Name("foo", lpar=(cst.LeftParen(),), rpar=(cst.RightParen(),)),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "Cannot have parens around Name",
            ),
            (
                lambda: cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    asynchronous=cst.Asynchronous(
                        whitespace_after=cst.SimpleWhitespace("")
                    ),
                ),
                "one space after Asynchronous",
            ),
            (
                lambda: cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    whitespace_after_def=cst.SimpleWhitespace(""),
                ),
                "one space between 'def' and name",
            ),
            (
                lambda: cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        star_kwarg=cst.Param(cst.Name("bar"), equal=cst.AssignEqual())
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "Must have a default when specifying an AssignEqual.",
            ),
            (
                lambda: cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(star_kwarg=cst.Param(cst.Name("bar"), star="***")),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                r"Must specify either '', '\*' or '\*\*' for star.",
            ),
            (
                lambda: cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("bar"), default=cst.SimpleString('"one"')
                            ),
                            cst.Param(cst.Name("bar")),
                        )
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "Cannot have param without defaults following a param with defaults.",
            ),
            (
                lambda: cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        posonly_params=(
                            cst.Param(
                                cst.Name("bar"), default=cst.SimpleString('"one"')
                            ),
                        ),
                        params=(cst.Param(cst.Name("bar")),),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "Cannot have param without defaults following a param with defaults.",
            ),
            (
                lambda: cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(star_arg=cst.ParamStar()),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "Must have at least one kwonly param if ParamStar is used.",
            ),
            (
                lambda: cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(posonly_ind=cst.ParamSlash()),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "Must have at least one posonly param if ParamSlash is used.",
            ),
            (
                lambda: cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(params=(cst.Param(cst.Name("bar"), star="*"),)),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "Expecting a star prefix of ''",
            ),
            (
                lambda: cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("bar"),
                                default=cst.SimpleString('"one"'),
                                star="*",
                            ),
                        )
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "Expecting a star prefix of ''",
            ),
            (
                lambda: cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        kwonly_params=(cst.Param(cst.Name("bar"), star="*"),)
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "Expecting a star prefix of ''",
            ),
            (
                lambda: cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(star_arg=cst.Param(cst.Name("bar"), star="**")),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                r"Expecting a star prefix of '\*'",
            ),
            (
                lambda: cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(star_kwarg=cst.Param(cst.Name("bar"), star="*")),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                r"Expecting a star prefix of '\*\*'",
            ),
        )
    )
    def test_invalid(
        self, get_node: Callable[[], cst.CSTNode], expected_re: str
    ) -> None:
        self.assert_invalid(get_node, expected_re)


def _parse_statement_force_38(code: str) -> cst.BaseCompoundStatement:
    statement = cst.parse_statement(
        code, config=cst.PartialParserConfig(python_version="3.8")
    )
    if not isinstance(statement, cst.BaseCompoundStatement):
        raise ValueError(
            "This function is expecting to parse compound statements only!"
        )
    return statement


class FunctionDefParserTest(CSTNodeTest):
    @data_provider(
        (
            # Simple function definition without any arguments or return
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "def foo(): pass\n",
            ),
            # Functiondef with a return annotation
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    returns=cst.Annotation(
                        cst.Name("str"),
                        whitespace_before_indicator=cst.SimpleWhitespace(" "),
                    ),
                ),
                "def foo() -> str: pass\n",
            ),
            # Async function definition.
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    asynchronous=cst.Asynchronous(),
                ),
                "async def foo(): pass\n",
            ),
            # Async function definition with annotation.
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    asynchronous=cst.Asynchronous(),
                    returns=cst.Annotation(
                        cst.Name("int"),
                        whitespace_before_indicator=cst.SimpleWhitespace(" "),
                    ),
                ),
                "async def foo() -> int: pass\n",
            ),
            # Test basic positional params
            (
                cst.FunctionDef(
                    cst.Name("foo"),
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
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "def foo(bar, baz): pass\n",
            ),
            # Typed positional params
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("bar"),
                                cst.Annotation(
                                    cst.Name("str"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("baz"),
                                cst.Annotation(
                                    cst.Name("int"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                star="",
                            ),
                        )
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "def foo(bar: str, baz: int): pass\n",
            ),
            # Test basic positional default params
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("bar"),
                                equal=cst.AssignEqual(),
                                default=cst.SimpleString('"one"'),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("baz"),
                                equal=cst.AssignEqual(),
                                default=cst.Integer("5"),
                                star="",
                            ),
                        )
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                'def foo(bar = "one", baz = 5): pass\n',
            ),
            # Typed positional default params
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("bar"),
                                cst.Annotation(
                                    cst.Name("str"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                equal=cst.AssignEqual(),
                                default=cst.SimpleString('"one"'),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("baz"),
                                cst.Annotation(
                                    cst.Name("int"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                equal=cst.AssignEqual(),
                                default=cst.Integer("5"),
                                star="",
                            ),
                        )
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                'def foo(bar: str = "one", baz: int = 5): pass\n',
            ),
            # Mixed positional and default params.
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("bar"),
                                cst.Annotation(
                                    cst.Name("str"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                default=None,
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("baz"),
                                cst.Annotation(
                                    cst.Name("int"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                equal=cst.AssignEqual(),
                                default=cst.Integer("5"),
                                star="",
                            ),
                        )
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "def foo(bar: str, baz: int = 5): pass\n",
            ),
            # Test kwonly params
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        star_arg=cst.ParamStar(),
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                equal=cst.AssignEqual(),
                                default=cst.SimpleString('"one"'),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(cst.Name("baz"), default=None, star=""),
                        ),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                'def foo(*, bar = "one", baz): pass\n',
            ),
            # Typed kwonly params
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        star_arg=cst.ParamStar(),
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                cst.Annotation(
                                    cst.Name("str"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                equal=cst.AssignEqual(),
                                default=cst.SimpleString('"one"'),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("baz"),
                                cst.Annotation(
                                    cst.Name("int"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                default=None,
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("biz"),
                                cst.Annotation(
                                    cst.Name("str"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                equal=cst.AssignEqual(),
                                default=cst.SimpleString('"two"'),
                                star="",
                            ),
                        ),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                'def foo(*, bar: str = "one", baz: int, biz: str = "two"): pass\n',
            ),
            # Mixed params and kwonly_params
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("first"),
                                annotation=None,
                                default=None,
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("second"),
                                annotation=None,
                                default=None,
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
                                cst.Annotation(
                                    cst.Name("str"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                equal=cst.AssignEqual(),
                                default=cst.SimpleString('"one"'),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("baz"),
                                cst.Annotation(
                                    cst.Name("int"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                default=None,
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("biz"),
                                cst.Annotation(
                                    cst.Name("str"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                equal=cst.AssignEqual(),
                                default=cst.SimpleString('"two"'),
                                star="",
                            ),
                        ),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                'def foo(first, second, *, bar: str = "one", baz: int, biz: str = "two"): pass\n',
            ),
            # Mixed params and kwonly_params
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("first"),
                                annotation=None,
                                equal=cst.AssignEqual(),
                                default=cst.Float("1.0"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("second"),
                                annotation=None,
                                equal=cst.AssignEqual(),
                                default=cst.Float("1.5"),
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
                                cst.Annotation(
                                    cst.Name("str"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                equal=cst.AssignEqual(),
                                default=cst.SimpleString('"one"'),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("baz"),
                                cst.Annotation(
                                    cst.Name("int"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                default=None,
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("biz"),
                                cst.Annotation(
                                    cst.Name("str"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                equal=cst.AssignEqual(),
                                default=cst.SimpleString('"two"'),
                                star="",
                            ),
                        ),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                'def foo(first = 1.0, second = 1.5, *, bar: str = "one", baz: int, biz: str = "two"): pass\n',
            ),
            # Mixed params, and kwonly_params
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("first"),
                                annotation=None,
                                default=None,
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("second"),
                                annotation=None,
                                default=None,
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("third"),
                                annotation=None,
                                equal=cst.AssignEqual(),
                                default=cst.Float("1.0"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("fourth"),
                                annotation=None,
                                equal=cst.AssignEqual(),
                                default=cst.Float("1.5"),
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
                                cst.Annotation(
                                    cst.Name("str"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                equal=cst.AssignEqual(),
                                default=cst.SimpleString('"one"'),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("baz"),
                                cst.Annotation(
                                    cst.Name("int"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                default=None,
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("biz"),
                                cst.Annotation(
                                    cst.Name("str"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                equal=cst.AssignEqual(),
                                default=cst.SimpleString('"two"'),
                                star="",
                            ),
                        ),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                'def foo(first, second, third = 1.0, fourth = 1.5, *, bar: str = "one", baz: int, biz: str = "two"): pass\n',
            ),
            # Test star_arg
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        star_arg=cst.Param(
                            cst.Name("params"), annotation=None, default=None, star="*"
                        )
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "def foo(*params): pass\n",
            ),
            # Typed star_arg, include kwonly_params
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        star_arg=cst.Param(
                            cst.Name("params"),
                            cst.Annotation(
                                cst.Name("str"),
                                whitespace_before_indicator=cst.SimpleWhitespace(""),
                            ),
                            default=None,
                            star="*",
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                cst.Annotation(
                                    cst.Name("str"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                equal=cst.AssignEqual(),
                                default=cst.SimpleString('"one"'),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("baz"),
                                cst.Annotation(
                                    cst.Name("int"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                default=None,
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("biz"),
                                cst.Annotation(
                                    cst.Name("str"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                equal=cst.AssignEqual(),
                                default=cst.SimpleString('"two"'),
                                star="",
                            ),
                        ),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                'def foo(*params: str, bar: str = "one", baz: int, biz: str = "two"): pass\n',
            ),
            # Mixed params star_arg and kwonly_params
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        params=(
                            cst.Param(
                                cst.Name("first"),
                                annotation=None,
                                default=None,
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("second"),
                                annotation=None,
                                default=None,
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("third"),
                                annotation=None,
                                equal=cst.AssignEqual(),
                                default=cst.Float("1.0"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("fourth"),
                                annotation=None,
                                equal=cst.AssignEqual(),
                                default=cst.Float("1.5"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                        ),
                        star_arg=cst.Param(
                            cst.Name("params"),
                            cst.Annotation(
                                cst.Name("str"),
                                whitespace_before_indicator=cst.SimpleWhitespace(""),
                            ),
                            default=None,
                            star="*",
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        kwonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                cst.Annotation(
                                    cst.Name("str"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                equal=cst.AssignEqual(),
                                default=cst.SimpleString('"one"'),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("baz"),
                                cst.Annotation(
                                    cst.Name("int"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                default=None,
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("biz"),
                                cst.Annotation(
                                    cst.Name("str"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                equal=cst.AssignEqual(),
                                default=cst.SimpleString('"two"'),
                                star="",
                            ),
                        ),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                'def foo(first, second, third = 1.0, fourth = 1.5, *params: str, bar: str = "one", baz: int, biz: str = "two"): pass\n',
            ),
            # Test star_arg and star_kwarg
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        star_kwarg=cst.Param(
                            cst.Name("kwparams"),
                            annotation=None,
                            default=None,
                            star="**",
                        )
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "def foo(**kwparams): pass\n",
            ),
            # Test star_arg and kwarg
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        star_arg=cst.Param(
                            cst.Name("params"),
                            annotation=None,
                            default=None,
                            star="*",
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        star_kwarg=cst.Param(
                            cst.Name("kwparams"),
                            annotation=None,
                            default=None,
                            star="**",
                        ),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "def foo(*params, **kwparams): pass\n",
            ),
            # Test typed star_arg and star_kwarg
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        star_arg=cst.Param(
                            cst.Name("params"),
                            cst.Annotation(
                                cst.Name("str"),
                                whitespace_before_indicator=cst.SimpleWhitespace(""),
                            ),
                            default=None,
                            star="*",
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        star_kwarg=cst.Param(
                            cst.Name("kwparams"),
                            cst.Annotation(
                                cst.Name("int"),
                                whitespace_before_indicator=cst.SimpleWhitespace(""),
                            ),
                            default=None,
                            star="**",
                        ),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "def foo(*params: str, **kwparams: int): pass\n",
            ),
            # Test decorators
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    (cst.Decorator(cst.Name("bar")),),
                ),
                "@bar\ndef foo(): pass\n",
            ),
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    (
                        cst.Decorator(
                            cst.Call(
                                cst.Name("bar"),
                                (
                                    cst.Arg(
                                        cst.Name("baz"),
                                        keyword=None,
                                        comma=cst.Comma(
                                            whitespace_after=cst.SimpleWhitespace(" ")
                                        ),
                                    ),
                                    cst.Arg(cst.SimpleString("'123'"), keyword=None),
                                ),
                            )
                        ),
                    ),
                ),
                "@bar(baz, '123')\ndef foo(): pass\n",
            ),
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    (
                        cst.Decorator(
                            cst.Call(
                                cst.Name("bar"),
                                (cst.Arg(cst.SimpleString("'123'"), keyword=None),),
                            )
                        ),
                        cst.Decorator(
                            cst.Call(
                                cst.Name("baz"),
                                (cst.Arg(cst.SimpleString("'456'"), keyword=None),),
                            )
                        ),
                    ),
                ),
                "@bar('123')\n@baz('456')\ndef foo(): pass\n",
            ),
            # Leading lines
            (
                cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    leading_lines=(
                        cst.EmptyLine(comment=cst.Comment("# leading comment")),
                    ),
                ),
                "# leading comment\ndef foo(): pass\n",
            ),
            # Inner whitespace
            (
                cst.FunctionDef(
                    leading_lines=(
                        cst.EmptyLine(),
                        cst.EmptyLine(
                            comment=cst.Comment("# What an amazing decorator")
                        ),
                    ),
                    decorators=(
                        cst.Decorator(
                            whitespace_after_at=cst.SimpleWhitespace(" "),
                            decorator=cst.Call(
                                func=cst.Name("bar"),
                                whitespace_after_func=cst.SimpleWhitespace(" "),
                                whitespace_before_args=cst.SimpleWhitespace("  "),
                            ),
                        ),
                    ),
                    lines_after_decorators=(
                        cst.EmptyLine(comment=cst.Comment("# What a great function")),
                    ),
                    asynchronous=cst.Asynchronous(
                        whitespace_after=cst.SimpleWhitespace("  ")
                    ),
                    whitespace_after_def=cst.SimpleWhitespace("  "),
                    name=cst.Name("foo"),
                    whitespace_after_name=cst.SimpleWhitespace("  "),
                    whitespace_before_params=cst.SimpleWhitespace("  "),
                    params=cst.Parameters(),
                    returns=cst.Annotation(
                        whitespace_before_indicator=cst.SimpleWhitespace("  "),
                        whitespace_after_indicator=cst.SimpleWhitespace("  "),
                        annotation=cst.Name("str"),
                    ),
                    whitespace_before_colon=cst.SimpleWhitespace(" "),
                    body=cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "\n# What an amazing decorator\n@ bar (  )\n# What a great function\nasync  def  foo  (  )  ->  str : pass\n",
            ),
        )
    )
    def test_valid(self, node: cst.CSTNode, code: str) -> None:
        self.validate_node(node, code, parse_statement)

    @data_provider(
        (
            # Test basic positional only params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
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
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "def foo(bar, baz, /): pass\n",
            },
            # Positional only params with whitespace after but no comma
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
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
                        posonly_ind=cst.ParamSlash(
                            whitespace_after=cst.SimpleWhitespace(" ")
                        ),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "def foo(bar, baz, / ): pass\n",
                "native_only": True,
            },
            # Typed positional only params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        posonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                cst.Annotation(
                                    cst.Name("str"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("baz"),
                                cst.Annotation(
                                    cst.Name("int"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                        ),
                        posonly_ind=cst.ParamSlash(),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "def foo(bar: str, baz: int, /): pass\n",
            },
            # Test basic positional only default params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        posonly_params=(
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
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                        ),
                        posonly_ind=cst.ParamSlash(),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": 'def foo(bar = "one", baz = 5, /): pass\n',
            },
            # Typed positional only default params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        posonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                cst.Annotation(
                                    cst.Name("str"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                default=cst.SimpleString('"one"'),
                                equal=cst.AssignEqual(),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                            cst.Param(
                                cst.Name("baz"),
                                cst.Annotation(
                                    cst.Name("int"),
                                    whitespace_before_indicator=cst.SimpleWhitespace(
                                        ""
                                    ),
                                ),
                                default=cst.Integer("5"),
                                equal=cst.AssignEqual(),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                        ),
                        posonly_ind=cst.ParamSlash(),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": 'def foo(bar: str = "one", baz: int = 5, /): pass\n',
            },
            # Test positional only params and positional params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        posonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                        ),
                        posonly_ind=cst.ParamSlash(
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        params=(
                            cst.Param(
                                cst.Name("baz"),
                                star="",
                            ),
                        ),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "def foo(bar, /, baz): pass\n",
            },
            # Test positional only params and star args
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        posonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                        ),
                        posonly_ind=cst.ParamSlash(
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        star_arg=cst.Param(cst.Name("baz"), star="*"),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "def foo(bar, /, *baz): pass\n",
            },
            # Test positional only params and kwonly params
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        posonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                        ),
                        posonly_ind=cst.ParamSlash(
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        star_arg=cst.ParamStar(
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        kwonly_params=(cst.Param(cst.Name("baz"), star=""),),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "def foo(bar, /, *, baz): pass\n",
            },
            # Test positional only params and star kwargs
            {
                "node": cst.FunctionDef(
                    cst.Name("foo"),
                    cst.Parameters(
                        posonly_params=(
                            cst.Param(
                                cst.Name("bar"),
                                star="",
                                comma=cst.Comma(
                                    whitespace_after=cst.SimpleWhitespace(" ")
                                ),
                            ),
                        ),
                        posonly_ind=cst.ParamSlash(
                            comma=cst.Comma(whitespace_after=cst.SimpleWhitespace(" ")),
                        ),
                        star_kwarg=cst.Param(cst.Name("baz"), star="**"),
                    ),
                    cst.SimpleStatementSuite((cst.Pass(),)),
                ),
                "code": "def foo(bar, /, **baz): pass\n",
            },
        )
    )
    def test_valid_38(self, node: cst.CSTNode, code: str, **kwargs: Any) -> None:
        if not is_native() and kwargs.get("native_only", False):
            self.skipTest("disabled for pure python parser")
        self.validate_node(node, code, _parse_statement_force_38)

    @data_provider(
        (
            {
                "code": "async def foo(): pass",
                "parser": parse_statement_as(python_version="3.7"),
                "expect_success": True,
            },
            {
                "code": "async def foo(): pass",
                "parser": parse_statement_as(python_version="3.6"),
                "expect_success": True,
            },
            {
                "code": "async def foo(): pass",
                "parser": parse_statement_as(python_version="3.5"),
                "expect_success": True,
            },
            {
                "code": "async def foo(): pass",
                "parser": parse_statement_as(python_version="3.3"),
                "expect_success": False,
            },
        )
    )
    def test_versions(self, **kwargs: Any) -> None:
        if is_native() and not kwargs.get("expect_success", True):
            self.skipTest("parse errors are disabled for native parser")
        self.assert_parses(**kwargs)

    @data_provider(
        (
            {"code": "A[:*b]"},
            {"code": "A[*b:]"},
            {"code": "A[*b:*b]"},
            {"code": "A[*(1:2)]"},
            {"code": "A[*:]"},
            {"code": "A[:*]"},
            {"code": "A[**b]"},
            {"code": "def f(x: *b): pass"},
            {"code": "def f(**x: *b): pass"},
            {"code": "x: *b"},
        )
    )
    def test_parse_error(self, **kwargs: Any) -> None:
        if not is_native():
            self.skipTest("Skipped for non-native parser")
        self.assert_parses(**kwargs, expect_success=False, parser=parse_statement)
