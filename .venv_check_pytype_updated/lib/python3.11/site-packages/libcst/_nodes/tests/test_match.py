# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any, Callable, Optional

import libcst as cst
from libcst import parse_statement
from libcst._nodes.tests.base import CSTNodeTest
from libcst._parser.entrypoints import is_native
from libcst.testing.utils import data_provider

parser: Optional[Callable[[str], cst.CSTNode]] = (
    parse_statement if is_native() else None
)


class MatchTest(CSTNodeTest):
    @data_provider(
        (
            # Values and singletons
            {
                "node": cst.Match(
                    subject=cst.Name("x"),
                    cases=[
                        cst.MatchCase(
                            pattern=cst.MatchSingleton(cst.Name("None")),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                        cst.MatchCase(
                            pattern=cst.MatchValue(cst.SimpleString('"foo"')),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                    ],
                ),
                "code": "match x:\n"
                + "    case None: pass\n"
                + '    case "foo": pass\n',
                "parser": parser,
            },
            # Parenthesized value
            {
                "node": cst.Match(
                    subject=cst.Name(
                        value="x",
                    ),
                    cases=[
                        cst.MatchCase(
                            pattern=cst.MatchAs(
                                pattern=cst.MatchValue(
                                    value=cst.Integer(
                                        value="1",
                                        lpar=[
                                            cst.LeftParen(),
                                        ],
                                        rpar=[
                                            cst.RightParen(),
                                        ],
                                    ),
                                ),
                                name=cst.Name(
                                    value="z",
                                ),
                                whitespace_before_as=cst.SimpleWhitespace(" "),
                                whitespace_after_as=cst.SimpleWhitespace(" "),
                            ),
                            body=cst.SimpleStatementSuite([cst.Pass()]),
                        ),
                    ],
                ),
                "code": "match x:\n    case (1) as z: pass\n",
                "parser": parser,
            },
            # List patterns
            {
                "node": cst.Match(
                    subject=cst.Name("x"),
                    cases=[
                        cst.MatchCase(  # empty list
                            pattern=cst.MatchList(
                                [],
                                lbracket=cst.LeftSquareBracket(),
                                rbracket=cst.RightSquareBracket(),
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                        cst.MatchCase(  # single element list
                            pattern=cst.MatchList(
                                [
                                    cst.MatchSequenceElement(
                                        cst.MatchSingleton(cst.Name("None"))
                                    )
                                ],
                                lbracket=cst.LeftSquareBracket(),
                                rbracket=cst.RightSquareBracket(),
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                        cst.MatchCase(  # single element list with trailing comma
                            pattern=cst.MatchList(
                                [
                                    cst.MatchSequenceElement(
                                        cst.MatchSingleton(cst.Name("None")),
                                        cst.Comma(),
                                    )
                                ],
                                lbracket=cst.LeftSquareBracket(),
                                rbracket=cst.RightSquareBracket(),
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                    ],
                ),
                "code": (
                    "match x:\n"
                    + "    case []: pass\n"
                    + "    case [None]: pass\n"
                    + "    case [None,]: pass\n"
                ),
                "parser": parser,
            },
            # Tuple patterns
            {
                "node": cst.Match(
                    subject=cst.Name("x"),
                    cases=[
                        cst.MatchCase(  # empty tuple
                            pattern=cst.MatchTuple(
                                [],
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                        cst.MatchCase(  # two element tuple
                            pattern=cst.MatchTuple(
                                [
                                    cst.MatchSequenceElement(
                                        cst.MatchSingleton(cst.Name("None")),
                                        cst.Comma(),
                                    ),
                                    cst.MatchSequenceElement(
                                        cst.MatchSingleton(cst.Name("None")),
                                    ),
                                ],
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                        cst.MatchCase(  # single element tuple with trailing comma
                            pattern=cst.MatchTuple(
                                [
                                    cst.MatchSequenceElement(
                                        cst.MatchSingleton(cst.Name("None")),
                                        cst.Comma(),
                                    )
                                ],
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                        cst.MatchCase(  # two element tuple
                            pattern=cst.MatchTuple(
                                [
                                    cst.MatchSequenceElement(
                                        cst.MatchSingleton(cst.Name("None")),
                                        cst.Comma(),
                                    ),
                                    cst.MatchStar(
                                        comma=cst.Comma(),
                                    ),
                                    cst.MatchSequenceElement(
                                        cst.MatchSingleton(cst.Name("None")),
                                    ),
                                ],
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                    ],
                ),
                "code": (
                    "match x:\n"
                    + "    case (): pass\n"
                    + "    case (None,None): pass\n"
                    + "    case (None,): pass\n"
                    + "    case (None,*_,None): pass\n"
                ),
                "parser": parser,
            },
            # Mapping patterns
            {
                "node": cst.Match(
                    subject=cst.Name("x"),
                    cases=[
                        cst.MatchCase(  # empty mapping
                            pattern=cst.MatchMapping(
                                [],
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                        cst.MatchCase(  # two element mapping
                            pattern=cst.MatchMapping(
                                [
                                    cst.MatchMappingElement(
                                        key=cst.SimpleString('"a"'),
                                        pattern=cst.MatchSingleton(cst.Name("None")),
                                        comma=cst.Comma(),
                                    ),
                                    cst.MatchMappingElement(
                                        key=cst.SimpleString('"b"'),
                                        pattern=cst.MatchSingleton(cst.Name("None")),
                                    ),
                                ],
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                        cst.MatchCase(  # single element mapping with trailing comma
                            pattern=cst.MatchMapping(
                                [
                                    cst.MatchMappingElement(
                                        key=cst.SimpleString('"a"'),
                                        pattern=cst.MatchSingleton(cst.Name("None")),
                                        comma=cst.Comma(),
                                    )
                                ],
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                        cst.MatchCase(  # rest
                            pattern=cst.MatchMapping(
                                rest=cst.Name("rest"),
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                    ],
                ),
                "code": (
                    "match x:\n"
                    + "    case {}: pass\n"
                    + '    case {"a": None,"b": None}: pass\n'
                    + '    case {"a": None,}: pass\n'
                    + "    case {**rest}: pass\n"
                ),
                "parser": parser,
            },
            # Class patterns
            {
                "node": cst.Match(
                    subject=cst.Name("x"),
                    cases=[
                        cst.MatchCase(  # empty class
                            pattern=cst.MatchClass(
                                cls=cst.Attribute(cst.Name("a"), cst.Name("b")),
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                        cst.MatchCase(  # single pattern class
                            pattern=cst.MatchClass(
                                cls=cst.Attribute(cst.Name("a"), cst.Name("b")),
                                patterns=[
                                    cst.MatchSequenceElement(
                                        cst.MatchSingleton(cst.Name("None"))
                                    )
                                ],
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                        cst.MatchCase(  # single pattern class with trailing comma
                            pattern=cst.MatchClass(
                                cls=cst.Attribute(cst.Name("a"), cst.Name("b")),
                                patterns=[
                                    cst.MatchSequenceElement(
                                        cst.MatchSingleton(cst.Name("None")),
                                        comma=cst.Comma(),
                                    )
                                ],
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                        cst.MatchCase(  # single keyword pattern class
                            pattern=cst.MatchClass(
                                cls=cst.Attribute(cst.Name("a"), cst.Name("b")),
                                kwds=[
                                    cst.MatchKeywordElement(
                                        key=cst.Name("foo"),
                                        pattern=cst.MatchSingleton(cst.Name("None")),
                                    )
                                ],
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                        cst.MatchCase(  # single keyword pattern class with trailing comma
                            pattern=cst.MatchClass(
                                cls=cst.Attribute(cst.Name("a"), cst.Name("b")),
                                kwds=[
                                    cst.MatchKeywordElement(
                                        key=cst.Name("foo"),
                                        pattern=cst.MatchSingleton(cst.Name("None")),
                                        comma=cst.Comma(),
                                    )
                                ],
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                        cst.MatchCase(  # now all at once
                            pattern=cst.MatchClass(
                                cls=cst.Attribute(cst.Name("a"), cst.Name("b")),
                                patterns=[
                                    cst.MatchSequenceElement(
                                        cst.MatchSingleton(cst.Name("None")),
                                        cst.Comma(),
                                    ),
                                    cst.MatchSequenceElement(
                                        cst.MatchSingleton(cst.Name("None")),
                                        cst.Comma(),
                                    ),
                                ],
                                kwds=[
                                    cst.MatchKeywordElement(
                                        key=cst.Name("foo"),
                                        pattern=cst.MatchSingleton(cst.Name("None")),
                                        comma=cst.Comma(),
                                    ),
                                    cst.MatchKeywordElement(
                                        key=cst.Name("bar"),
                                        pattern=cst.MatchSingleton(cst.Name("None")),
                                        comma=cst.Comma(),
                                    ),
                                ],
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                    ],
                ),
                "code": (
                    "match x:\n"
                    + "    case a.b(): pass\n"
                    + "    case a.b(None): pass\n"
                    + "    case a.b(None,): pass\n"
                    + "    case a.b(foo=None): pass\n"
                    + "    case a.b(foo=None,): pass\n"
                    + "    case a.b(None,None,foo=None,bar=None,): pass\n"
                ),
                "parser": parser,
            },
            # as pattern
            {
                "node": cst.Match(
                    subject=cst.Name("x"),
                    cases=[
                        cst.MatchCase(
                            pattern=cst.MatchAs(),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                        cst.MatchCase(
                            pattern=cst.MatchAs(name=cst.Name("foo")),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                        cst.MatchCase(
                            pattern=cst.MatchAs(
                                pattern=cst.MatchSingleton(cst.Name("None")),
                                name=cst.Name("bar"),
                                whitespace_before_as=cst.SimpleWhitespace(" "),
                                whitespace_after_as=cst.SimpleWhitespace(" "),
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                    ],
                ),
                "code": "match x:\n"
                + "    case _: pass\n"
                + "    case foo: pass\n"
                + "    case None as bar: pass\n",
                "parser": parser,
            },
            # or pattern
            {
                "node": cst.Match(
                    subject=cst.Name("x"),
                    cases=[
                        cst.MatchCase(
                            pattern=cst.MatchOr(
                                [
                                    cst.MatchOrElement(
                                        cst.MatchSingleton(cst.Name("None")),
                                        cst.BitOr(),
                                    ),
                                    cst.MatchOrElement(
                                        cst.MatchSingleton(cst.Name("False")),
                                        cst.BitOr(),
                                    ),
                                    cst.MatchOrElement(
                                        cst.MatchSingleton(cst.Name("True"))
                                    ),
                                ]
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        )
                    ],
                ),
                "code": "match x:\n    case None | False | True: pass\n",
                "parser": parser,
            },
            {  # exercise sentinels
                "node": cst.Match(
                    subject=cst.Name("x"),
                    cases=[
                        cst.MatchCase(
                            pattern=cst.MatchList(
                                [cst.MatchStar(), cst.MatchStar()],
                                lbracket=None,
                                rbracket=None,
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                        cst.MatchCase(
                            pattern=cst.MatchTuple(
                                [
                                    cst.MatchSequenceElement(
                                        cst.MatchSingleton(cst.Name("None"))
                                    )
                                ]
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                        cst.MatchCase(
                            pattern=cst.MatchAs(
                                pattern=cst.MatchTuple(
                                    [
                                        cst.MatchSequenceElement(
                                            cst.MatchSingleton(cst.Name("None"))
                                        )
                                    ]
                                ),
                                name=cst.Name("bar"),
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                        cst.MatchCase(
                            pattern=cst.MatchOr(
                                [
                                    cst.MatchOrElement(
                                        cst.MatchSingleton(cst.Name("None")),
                                    ),
                                    cst.MatchOrElement(
                                        cst.MatchSingleton(cst.Name("False")),
                                    ),
                                    cst.MatchOrElement(
                                        cst.MatchSingleton(cst.Name("True"))
                                    ),
                                ]
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                    ],
                ),
                "code": "match x:\n"
                + "    case *_, *_: pass\n"
                + "    case (None,): pass\n"
                + "    case (None,) as bar: pass\n"
                + "    case None | False | True: pass\n",
                "parser": None,
            },
            # Match without whitespace between keyword and the expr
            {
                "node": cst.Match(
                    subject=cst.Name(
                        "x", lpar=[cst.LeftParen()], rpar=[cst.RightParen()]
                    ),
                    cases=[
                        cst.MatchCase(
                            pattern=cst.MatchSingleton(
                                cst.Name(
                                    "None",
                                    lpar=[cst.LeftParen()],
                                    rpar=[cst.RightParen()],
                                )
                            ),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                            whitespace_after_case=cst.SimpleWhitespace(
                                value="",
                            ),
                        ),
                    ],
                    whitespace_after_match=cst.SimpleWhitespace(
                        value="",
                    ),
                ),
                "code": "match(x):\n    case(None): pass\n",
                "parser": parser,
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)
