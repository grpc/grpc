# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any, Callable, Optional

import libcst as cst
from libcst import parse_statement
from libcst._nodes.tests.base import CSTNodeTest, DummyIndentedBlock
from libcst._parser.entrypoints import is_native
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider

native_parse_statement: Optional[Callable[[str], cst.CSTNode]] = (
    parse_statement if is_native() else None
)


class TryTest(CSTNodeTest):
    @data_provider(
        (
            # Simple try/except block
            {
                "node": cst.Try(
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    handlers=(
                        cst.ExceptHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            whitespace_after_except=cst.SimpleWhitespace(""),
                        ),
                    ),
                ),
                "code": "try: pass\nexcept: pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (2, 12)),
            },
            # Try/except with a class
            {
                "node": cst.Try(
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    handlers=(
                        cst.ExceptHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            type=cst.Name("Exception"),
                        ),
                    ),
                ),
                "code": "try: pass\nexcept Exception: pass\n",
                "parser": parse_statement,
            },
            # Try/except with a named class
            {
                "node": cst.Try(
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    handlers=(
                        cst.ExceptHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            type=cst.Name("Exception"),
                            name=cst.AsName(cst.Name("exc")),
                        ),
                    ),
                ),
                "code": "try: pass\nexcept Exception as exc: pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (2, 29)),
            },
            # Try/except with multiple clauses
            {
                "node": cst.Try(
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    handlers=(
                        cst.ExceptHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            type=cst.Name("TypeError"),
                            name=cst.AsName(cst.Name("e")),
                        ),
                        cst.ExceptHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            type=cst.Name("KeyError"),
                            name=cst.AsName(cst.Name("e")),
                        ),
                        cst.ExceptHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            whitespace_after_except=cst.SimpleWhitespace(""),
                        ),
                    ),
                ),
                "code": "try: pass\n"
                + "except TypeError as e: pass\n"
                + "except KeyError as e: pass\n"
                + "except: pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (4, 12)),
            },
            # Simple try/finally block
            {
                "node": cst.Try(
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    finalbody=cst.Finally(cst.SimpleStatementSuite((cst.Pass(),))),
                ),
                "code": "try: pass\nfinally: pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (2, 13)),
            },
            # Simple try/except/finally block
            {
                "node": cst.Try(
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    handlers=(
                        cst.ExceptHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            whitespace_after_except=cst.SimpleWhitespace(""),
                        ),
                    ),
                    finalbody=cst.Finally(cst.SimpleStatementSuite((cst.Pass(),))),
                ),
                "code": "try: pass\nexcept: pass\nfinally: pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (3, 13)),
            },
            # Simple try/except/else block
            {
                "node": cst.Try(
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    handlers=(
                        cst.ExceptHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            whitespace_after_except=cst.SimpleWhitespace(""),
                        ),
                    ),
                    orelse=cst.Else(cst.SimpleStatementSuite((cst.Pass(),))),
                ),
                "code": "try: pass\nexcept: pass\nelse: pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (3, 10)),
            },
            # Simple try/except/else block/finally
            {
                "node": cst.Try(
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    handlers=(
                        cst.ExceptHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            whitespace_after_except=cst.SimpleWhitespace(""),
                        ),
                    ),
                    orelse=cst.Else(cst.SimpleStatementSuite((cst.Pass(),))),
                    finalbody=cst.Finally(cst.SimpleStatementSuite((cst.Pass(),))),
                ),
                "code": "try: pass\nexcept: pass\nelse: pass\nfinally: pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (4, 13)),
            },
            # Verify whitespace in various locations
            {
                "node": cst.Try(
                    leading_lines=(cst.EmptyLine(comment=cst.Comment("# 1")),),
                    body=cst.SimpleStatementSuite((cst.Pass(),)),
                    handlers=(
                        cst.ExceptHandler(
                            leading_lines=(cst.EmptyLine(comment=cst.Comment("# 2")),),
                            type=cst.Name("TypeError"),
                            name=cst.AsName(
                                cst.Name("e"),
                                whitespace_before_as=cst.SimpleWhitespace("  "),
                                whitespace_after_as=cst.SimpleWhitespace("  "),
                            ),
                            whitespace_after_except=cst.SimpleWhitespace("  "),
                            whitespace_before_colon=cst.SimpleWhitespace(" "),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                    ),
                    orelse=cst.Else(
                        leading_lines=(cst.EmptyLine(comment=cst.Comment("# 3")),),
                        body=cst.SimpleStatementSuite((cst.Pass(),)),
                        whitespace_before_colon=cst.SimpleWhitespace(" "),
                    ),
                    finalbody=cst.Finally(
                        leading_lines=(cst.EmptyLine(comment=cst.Comment("# 4")),),
                        body=cst.SimpleStatementSuite((cst.Pass(),)),
                        whitespace_before_colon=cst.SimpleWhitespace(" "),
                    ),
                    whitespace_before_colon=cst.SimpleWhitespace(" "),
                ),
                "code": "# 1\ntry : pass\n# 2\nexcept  TypeError  as  e : pass\n# 3\nelse : pass\n# 4\nfinally : pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((2, 0), (8, 14)),
            },
            # Please don't write code like this
            {
                "node": cst.Try(
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    handlers=(
                        cst.ExceptHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            type=cst.Name("TypeError"),
                            name=cst.AsName(cst.Name("e")),
                        ),
                        cst.ExceptHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            type=cst.Name("KeyError"),
                            name=cst.AsName(cst.Name("e")),
                        ),
                        cst.ExceptHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            whitespace_after_except=cst.SimpleWhitespace(""),
                        ),
                    ),
                    orelse=cst.Else(cst.SimpleStatementSuite((cst.Pass(),))),
                    finalbody=cst.Finally(cst.SimpleStatementSuite((cst.Pass(),))),
                ),
                "code": "try: pass\n"
                + "except TypeError as e: pass\n"
                + "except KeyError as e: pass\n"
                + "except: pass\n"
                + "else: pass\n"
                + "finally: pass\n",
                "parser": parse_statement,
                "expected_position": CodeRange((1, 0), (6, 13)),
            },
            # Verify indentation
            {
                "node": DummyIndentedBlock(
                    "    ",
                    cst.Try(
                        cst.SimpleStatementSuite((cst.Pass(),)),
                        handlers=(
                            cst.ExceptHandler(
                                cst.SimpleStatementSuite((cst.Pass(),)),
                                type=cst.Name("TypeError"),
                                name=cst.AsName(cst.Name("e")),
                            ),
                            cst.ExceptHandler(
                                cst.SimpleStatementSuite((cst.Pass(),)),
                                type=cst.Name("KeyError"),
                                name=cst.AsName(cst.Name("e")),
                            ),
                            cst.ExceptHandler(
                                cst.SimpleStatementSuite((cst.Pass(),)),
                                whitespace_after_except=cst.SimpleWhitespace(""),
                            ),
                        ),
                        orelse=cst.Else(cst.SimpleStatementSuite((cst.Pass(),))),
                        finalbody=cst.Finally(cst.SimpleStatementSuite((cst.Pass(),))),
                    ),
                ),
                "code": "    try: pass\n"
                + "    except TypeError as e: pass\n"
                + "    except KeyError as e: pass\n"
                + "    except: pass\n"
                + "    else: pass\n"
                + "    finally: pass\n",
                "parser": None,
            },
            # Verify indentation in bodies
            {
                "node": DummyIndentedBlock(
                    "    ",
                    cst.Try(
                        cst.IndentedBlock((cst.SimpleStatementLine((cst.Pass(),)),)),
                        handlers=(
                            cst.ExceptHandler(
                                cst.IndentedBlock(
                                    (cst.SimpleStatementLine((cst.Pass(),)),)
                                ),
                                whitespace_after_except=cst.SimpleWhitespace(""),
                            ),
                        ),
                        orelse=cst.Else(
                            cst.IndentedBlock((cst.SimpleStatementLine((cst.Pass(),)),))
                        ),
                        finalbody=cst.Finally(
                            cst.IndentedBlock((cst.SimpleStatementLine((cst.Pass(),)),))
                        ),
                    ),
                ),
                "code": "    try:\n"
                + "        pass\n"
                + "    except:\n"
                + "        pass\n"
                + "    else:\n"
                + "        pass\n"
                + "    finally:\n"
                + "        pass\n",
                "parser": None,
            },
            # No space when using grouping parens
            {
                "node": cst.Try(
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    handlers=(
                        cst.ExceptHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            whitespace_after_except=cst.SimpleWhitespace(""),
                            type=cst.Name(
                                "Exception",
                                lpar=(cst.LeftParen(),),
                                rpar=(cst.RightParen(),),
                            ),
                        ),
                    ),
                ),
                "code": "try: pass\nexcept(Exception): pass\n",
                "parser": parse_statement,
            },
            # No space when using tuple
            {
                "node": cst.Try(
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    handlers=(
                        cst.ExceptHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            whitespace_after_except=cst.SimpleWhitespace(""),
                            type=cst.Tuple(
                                [
                                    cst.Element(
                                        cst.Name("IOError"),
                                        comma=cst.Comma(
                                            whitespace_after=cst.SimpleWhitespace(" ")
                                        ),
                                    ),
                                    cst.Element(cst.Name("ImportError")),
                                ]
                            ),
                        ),
                    ),
                ),
                "code": "try: pass\nexcept(IOError, ImportError): pass\n",
                "parser": parse_statement,
            },
            # No space before as
            {
                "node": cst.Try(
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    handlers=[
                        cst.ExceptHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            whitespace_after_except=cst.SimpleWhitespace(" "),
                            type=cst.Call(cst.Name("foo")),
                            name=cst.AsName(
                                whitespace_before_as=cst.SimpleWhitespace(""),
                                name=cst.Name("bar"),
                            ),
                        )
                    ],
                ),
                "code": "try: pass\nexcept foo()as bar: pass\n",
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            {
                "get_node": lambda: cst.AsName(cst.Name("")),
                "expected_re": "empty name identifier",
            },
            {
                "get_node": lambda: cst.AsName(
                    cst.Name("bla"), whitespace_after_as=cst.SimpleWhitespace("")
                ),
                "expected_re": "between 'as'",
            },
            {
                "get_node": lambda: cst.ExceptHandler(
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    name=cst.AsName(cst.Name("bla")),
                ),
                "expected_re": "name for an empty type",
            },
            {
                "get_node": lambda: cst.ExceptHandler(
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    type=cst.Name("TypeError"),
                    whitespace_after_except=cst.SimpleWhitespace(""),
                ),
                "expected_re": "at least one space after except",
            },
            {
                "get_node": lambda: cst.Try(cst.SimpleStatementSuite((cst.Pass(),))),
                "expected_re": "at least one ExceptHandler or Finally",
            },
            {
                "get_node": lambda: cst.Try(
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    orelse=cst.Else(cst.SimpleStatementSuite((cst.Pass(),))),
                    finalbody=cst.Finally(cst.SimpleStatementSuite((cst.Pass(),))),
                ),
                "expected_re": "at least one ExceptHandler in order to have an Else",
            },
            {
                "get_node": lambda: cst.Try(
                    body=cst.SimpleStatementSuite(body=[cst.Pass()]),
                    handlers=(
                        cst.ExceptHandler(
                            body=cst.SimpleStatementSuite(body=[cst.Pass()]),
                        ),
                        cst.ExceptHandler(
                            body=cst.SimpleStatementSuite(body=[cst.Pass()]),
                        ),
                    ),
                ),
                "expected_re": "The bare except: handler must be the last one.",
            },
            {
                "get_node": lambda: cst.Try(
                    body=cst.SimpleStatementSuite(body=[cst.Pass()]),
                    handlers=(
                        cst.ExceptHandler(
                            body=cst.SimpleStatementSuite(body=[cst.Pass()]),
                        ),
                        cst.ExceptHandler(
                            body=cst.SimpleStatementSuite(body=[cst.Pass()]),
                            type=cst.Name("Exception"),
                        ),
                    ),
                ),
                "expected_re": "The bare except: handler must be the last one.",
            },
        )
    )
    def test_invalid(self, **kwargs: Any) -> None:
        self.assert_invalid(**kwargs)


class TryStarTest(CSTNodeTest):
    @data_provider(
        (
            # Try/except with a class
            {
                "node": cst.TryStar(
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    handlers=(
                        cst.ExceptStarHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            type=cst.Name("Exception"),
                        ),
                    ),
                ),
                "code": "try: pass\nexcept* Exception: pass\n",
                "parser": native_parse_statement,
            },
            # Try/except with a named class
            {
                "node": cst.TryStar(
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    handlers=(
                        cst.ExceptStarHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            type=cst.Name("Exception"),
                            name=cst.AsName(cst.Name("exc")),
                        ),
                    ),
                ),
                "code": "try: pass\nexcept* Exception as exc: pass\n",
                "parser": native_parse_statement,
                "expected_position": CodeRange((1, 0), (2, 30)),
            },
            # Try/except with multiple clauses
            {
                "node": cst.TryStar(
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    handlers=(
                        cst.ExceptStarHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            type=cst.Name("TypeError"),
                            name=cst.AsName(cst.Name("e")),
                        ),
                        cst.ExceptStarHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            type=cst.Name("KeyError"),
                            name=cst.AsName(cst.Name("e")),
                        ),
                    ),
                ),
                "code": "try: pass\n"
                + "except* TypeError as e: pass\n"
                + "except* KeyError as e: pass\n",
                "parser": native_parse_statement,
                "expected_position": CodeRange((1, 0), (3, 27)),
            },
            # Simple try/except/finally block
            {
                "node": cst.TryStar(
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    handlers=(
                        cst.ExceptStarHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            type=cst.Name("KeyError"),
                            whitespace_after_except=cst.SimpleWhitespace(""),
                        ),
                    ),
                    finalbody=cst.Finally(cst.SimpleStatementSuite((cst.Pass(),))),
                ),
                "code": "try: pass\nexcept* KeyError: pass\nfinally: pass\n",
                "parser": native_parse_statement,
                "expected_position": CodeRange((1, 0), (3, 13)),
            },
            # Simple try/except/else block
            {
                "node": cst.TryStar(
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    handlers=(
                        cst.ExceptStarHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            type=cst.Name("KeyError"),
                            whitespace_after_except=cst.SimpleWhitespace(""),
                        ),
                    ),
                    orelse=cst.Else(cst.SimpleStatementSuite((cst.Pass(),))),
                ),
                "code": "try: pass\nexcept* KeyError: pass\nelse: pass\n",
                "parser": native_parse_statement,
                "expected_position": CodeRange((1, 0), (3, 10)),
            },
            # Verify whitespace in various locations
            {
                "node": cst.TryStar(
                    leading_lines=(cst.EmptyLine(comment=cst.Comment("# 1")),),
                    body=cst.SimpleStatementSuite((cst.Pass(),)),
                    handlers=(
                        cst.ExceptStarHandler(
                            leading_lines=(cst.EmptyLine(comment=cst.Comment("# 2")),),
                            type=cst.Name("TypeError"),
                            name=cst.AsName(
                                cst.Name("e"),
                                whitespace_before_as=cst.SimpleWhitespace("  "),
                                whitespace_after_as=cst.SimpleWhitespace("  "),
                            ),
                            whitespace_after_except=cst.SimpleWhitespace("  "),
                            whitespace_after_star=cst.SimpleWhitespace(""),
                            whitespace_before_colon=cst.SimpleWhitespace(" "),
                            body=cst.SimpleStatementSuite((cst.Pass(),)),
                        ),
                    ),
                    orelse=cst.Else(
                        leading_lines=(cst.EmptyLine(comment=cst.Comment("# 3")),),
                        body=cst.SimpleStatementSuite((cst.Pass(),)),
                        whitespace_before_colon=cst.SimpleWhitespace(" "),
                    ),
                    finalbody=cst.Finally(
                        leading_lines=(cst.EmptyLine(comment=cst.Comment("# 4")),),
                        body=cst.SimpleStatementSuite((cst.Pass(),)),
                        whitespace_before_colon=cst.SimpleWhitespace(" "),
                    ),
                    whitespace_before_colon=cst.SimpleWhitespace(" "),
                ),
                "code": "# 1\ntry : pass\n# 2\nexcept  *TypeError  as  e : pass\n# 3\nelse : pass\n# 4\nfinally : pass\n",
                "parser": native_parse_statement,
                "expected_position": CodeRange((2, 0), (8, 14)),
            },
            # Now all together
            {
                "node": cst.TryStar(
                    cst.SimpleStatementSuite((cst.Pass(),)),
                    handlers=(
                        cst.ExceptStarHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            type=cst.Name("TypeError"),
                            name=cst.AsName(cst.Name("e")),
                        ),
                        cst.ExceptStarHandler(
                            cst.SimpleStatementSuite((cst.Pass(),)),
                            type=cst.Name("KeyError"),
                            name=cst.AsName(cst.Name("e")),
                        ),
                    ),
                    orelse=cst.Else(cst.SimpleStatementSuite((cst.Pass(),))),
                    finalbody=cst.Finally(cst.SimpleStatementSuite((cst.Pass(),))),
                ),
                "code": "try: pass\n"
                + "except* TypeError as e: pass\n"
                + "except* KeyError as e: pass\n"
                + "else: pass\n"
                + "finally: pass\n",
                "parser": native_parse_statement,
                "expected_position": CodeRange((1, 0), (5, 13)),
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)
