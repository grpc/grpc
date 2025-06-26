# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
# pyre-unsafe

from typing import Sequence

from libcst._exceptions import ParserSyntaxError
from libcst._parser.parso.python.token import PythonTokenTypes
from libcst._parser.parso.utils import parse_version_string, PythonVersionInfo
from libcst._parser.types.whitespace_state import WhitespaceState
from libcst._parser.wrapped_tokenize import Token, tokenize
from libcst.testing.utils import data_provider, UnitTest

_PY38 = parse_version_string("3.8.0")
_PY37 = parse_version_string("3.7.0")
_PY36 = parse_version_string("3.6.0")
_PY35 = parse_version_string("3.5.0")


class WrappedTokenizeTest(UnitTest):
    maxDiff = 10000

    @data_provider(
        {
            "simple_py35": (
                "pass;\n",
                _PY35,
                (
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="pass",
                        start_pos=(1, 0),
                        end_pos=(1, 4),
                        whitespace_before=WhitespaceState(
                            line=1, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=4, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.OP,
                        string=";",
                        start_pos=(1, 4),
                        end_pos=(1, 5),
                        whitespace_before=WhitespaceState(
                            line=1, column=4, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=5, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NEWLINE,
                        string="\n",
                        start_pos=(1, 5),
                        end_pos=(2, 0),
                        whitespace_before=WhitespaceState(
                            line=1, column=5, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=2, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.ENDMARKER,
                        string="",
                        start_pos=(2, 0),
                        end_pos=(2, 0),
                        whitespace_before=WhitespaceState(
                            line=2, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=2, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                ),
            ),
            "with_indent_py35": (
                "if foo:\n    bar\n",
                _PY35,
                (
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="if",
                        start_pos=(1, 0),
                        end_pos=(1, 2),
                        whitespace_before=WhitespaceState(
                            line=1, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=2, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="foo",
                        start_pos=(1, 3),
                        end_pos=(1, 6),
                        whitespace_before=WhitespaceState(
                            line=1, column=2, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=6, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.OP,
                        string=":",
                        start_pos=(1, 6),
                        end_pos=(1, 7),
                        whitespace_before=WhitespaceState(
                            line=1, column=6, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=7, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NEWLINE,
                        string="\n",
                        start_pos=(1, 7),
                        end_pos=(2, 0),
                        whitespace_before=WhitespaceState(
                            line=1, column=7, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.INDENT,
                        string="",
                        start_pos=(2, 4),
                        end_pos=(2, 4),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent="    ",
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="bar",
                        start_pos=(2, 4),
                        end_pos=(2, 7),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=7,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NEWLINE,
                        string="\n",
                        start_pos=(2, 7),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=7,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.DEDENT,
                        string="",
                        start_pos=(3, 0),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.ENDMARKER,
                        string="",
                        start_pos=(3, 0),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                ),
            ),
            "async_py35": (
                "async def foo():\n    return await bar\n",
                _PY35,
                (
                    Token(
                        type=PythonTokenTypes.ASYNC,
                        string="async",
                        start_pos=(1, 0),
                        end_pos=(1, 5),
                        whitespace_before=WhitespaceState(
                            line=1, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=5, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="def",
                        start_pos=(1, 6),
                        end_pos=(1, 9),
                        whitespace_before=WhitespaceState(
                            line=1, column=5, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=9, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="foo",
                        start_pos=(1, 10),
                        end_pos=(1, 13),
                        whitespace_before=WhitespaceState(
                            line=1, column=9, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1,
                            column=13,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.OP,
                        string="(",
                        start_pos=(1, 13),
                        end_pos=(1, 14),
                        whitespace_before=WhitespaceState(
                            line=1,
                            column=13,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=14, absolute_indent="", is_parenthesized=True
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.OP,
                        string=")",
                        start_pos=(1, 14),
                        end_pos=(1, 15),
                        whitespace_before=WhitespaceState(
                            line=1, column=14, absolute_indent="", is_parenthesized=True
                        ),
                        whitespace_after=WhitespaceState(
                            line=1,
                            column=15,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.OP,
                        string=":",
                        start_pos=(1, 15),
                        end_pos=(1, 16),
                        whitespace_before=WhitespaceState(
                            line=1,
                            column=15,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=1,
                            column=16,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NEWLINE,
                        string="\n",
                        start_pos=(1, 16),
                        end_pos=(2, 0),
                        whitespace_before=WhitespaceState(
                            line=1,
                            column=16,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.INDENT,
                        string="",
                        start_pos=(2, 4),
                        end_pos=(2, 4),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent="    ",
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="return",
                        start_pos=(2, 4),
                        end_pos=(2, 10),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=10,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.AWAIT,
                        string="await",
                        start_pos=(2, 11),
                        end_pos=(2, 16),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=10,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=16,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="bar",
                        start_pos=(2, 17),
                        end_pos=(2, 20),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=16,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=20,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NEWLINE,
                        string="\n",
                        start_pos=(2, 20),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=20,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.DEDENT,
                        string="",
                        start_pos=(3, 0),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.ENDMARKER,
                        string="",
                        start_pos=(3, 0),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                ),
            ),
            "async_no_token_35": (
                "async;\n",
                _PY35,
                (
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="async",
                        start_pos=(1, 0),
                        end_pos=(1, 5),
                        whitespace_before=WhitespaceState(
                            line=1, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=5, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.OP,
                        string=";",
                        start_pos=(1, 5),
                        end_pos=(1, 6),
                        whitespace_before=WhitespaceState(
                            line=1, column=5, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=6, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NEWLINE,
                        string="\n",
                        start_pos=(1, 6),
                        end_pos=(2, 0),
                        whitespace_before=WhitespaceState(
                            line=1, column=6, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=2, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.ENDMARKER,
                        string="",
                        start_pos=(2, 0),
                        end_pos=(2, 0),
                        whitespace_before=WhitespaceState(
                            line=2, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=2, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                ),
            ),
            "simple_py36": (
                "pass;\n",
                _PY36,
                (
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="pass",
                        start_pos=(1, 0),
                        end_pos=(1, 4),
                        whitespace_before=WhitespaceState(
                            line=1, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=4, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.OP,
                        string=";",
                        start_pos=(1, 4),
                        end_pos=(1, 5),
                        whitespace_before=WhitespaceState(
                            line=1, column=4, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=5, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NEWLINE,
                        string="\n",
                        start_pos=(1, 5),
                        end_pos=(2, 0),
                        whitespace_before=WhitespaceState(
                            line=1, column=5, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=2, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.ENDMARKER,
                        string="",
                        start_pos=(2, 0),
                        end_pos=(2, 0),
                        whitespace_before=WhitespaceState(
                            line=2, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=2, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                ),
            ),
            "with_indent_py36": (
                "if foo:\n    bar\n",
                _PY36,
                (
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="if",
                        start_pos=(1, 0),
                        end_pos=(1, 2),
                        whitespace_before=WhitespaceState(
                            line=1, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=2, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="foo",
                        start_pos=(1, 3),
                        end_pos=(1, 6),
                        whitespace_before=WhitespaceState(
                            line=1, column=2, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=6, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.OP,
                        string=":",
                        start_pos=(1, 6),
                        end_pos=(1, 7),
                        whitespace_before=WhitespaceState(
                            line=1, column=6, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=7, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NEWLINE,
                        string="\n",
                        start_pos=(1, 7),
                        end_pos=(2, 0),
                        whitespace_before=WhitespaceState(
                            line=1, column=7, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.INDENT,
                        string="",
                        start_pos=(2, 4),
                        end_pos=(2, 4),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent="    ",
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="bar",
                        start_pos=(2, 4),
                        end_pos=(2, 7),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=7,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NEWLINE,
                        string="\n",
                        start_pos=(2, 7),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=7,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.DEDENT,
                        string="",
                        start_pos=(3, 0),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.ENDMARKER,
                        string="",
                        start_pos=(3, 0),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                ),
            ),
            "async_py36": (
                "async def foo():\n    return await bar\n",
                _PY36,
                (
                    Token(
                        type=PythonTokenTypes.ASYNC,
                        string="async",
                        start_pos=(1, 0),
                        end_pos=(1, 5),
                        whitespace_before=WhitespaceState(
                            line=1, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=5, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="def",
                        start_pos=(1, 6),
                        end_pos=(1, 9),
                        whitespace_before=WhitespaceState(
                            line=1, column=5, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=9, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="foo",
                        start_pos=(1, 10),
                        end_pos=(1, 13),
                        whitespace_before=WhitespaceState(
                            line=1, column=9, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1,
                            column=13,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.OP,
                        string="(",
                        start_pos=(1, 13),
                        end_pos=(1, 14),
                        whitespace_before=WhitespaceState(
                            line=1,
                            column=13,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=14, absolute_indent="", is_parenthesized=True
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.OP,
                        string=")",
                        start_pos=(1, 14),
                        end_pos=(1, 15),
                        whitespace_before=WhitespaceState(
                            line=1, column=14, absolute_indent="", is_parenthesized=True
                        ),
                        whitespace_after=WhitespaceState(
                            line=1,
                            column=15,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.OP,
                        string=":",
                        start_pos=(1, 15),
                        end_pos=(1, 16),
                        whitespace_before=WhitespaceState(
                            line=1,
                            column=15,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=1,
                            column=16,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NEWLINE,
                        string="\n",
                        start_pos=(1, 16),
                        end_pos=(2, 0),
                        whitespace_before=WhitespaceState(
                            line=1,
                            column=16,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.INDENT,
                        string="",
                        start_pos=(2, 4),
                        end_pos=(2, 4),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent="    ",
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="return",
                        start_pos=(2, 4),
                        end_pos=(2, 10),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=10,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.AWAIT,
                        string="await",
                        start_pos=(2, 11),
                        end_pos=(2, 16),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=10,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=16,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="bar",
                        start_pos=(2, 17),
                        end_pos=(2, 20),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=16,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=20,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NEWLINE,
                        string="\n",
                        start_pos=(2, 20),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=20,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.DEDENT,
                        string="",
                        start_pos=(3, 0),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.ENDMARKER,
                        string="",
                        start_pos=(3, 0),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                ),
            ),
            "async_no_token_36": (
                "async;\n",
                _PY36,
                (
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="async",
                        start_pos=(1, 0),
                        end_pos=(1, 5),
                        whitespace_before=WhitespaceState(
                            line=1, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=5, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.OP,
                        string=";",
                        start_pos=(1, 5),
                        end_pos=(1, 6),
                        whitespace_before=WhitespaceState(
                            line=1, column=5, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=6, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NEWLINE,
                        string="\n",
                        start_pos=(1, 6),
                        end_pos=(2, 0),
                        whitespace_before=WhitespaceState(
                            line=1, column=6, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=2, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.ENDMARKER,
                        string="",
                        start_pos=(2, 0),
                        end_pos=(2, 0),
                        whitespace_before=WhitespaceState(
                            line=2, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=2, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                ),
            ),
            "simple_py37": (
                "pass;\n",
                _PY37,
                (
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="pass",
                        start_pos=(1, 0),
                        end_pos=(1, 4),
                        whitespace_before=WhitespaceState(
                            line=1, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=4, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.OP,
                        string=";",
                        start_pos=(1, 4),
                        end_pos=(1, 5),
                        whitespace_before=WhitespaceState(
                            line=1, column=4, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=5, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NEWLINE,
                        string="\n",
                        start_pos=(1, 5),
                        end_pos=(2, 0),
                        whitespace_before=WhitespaceState(
                            line=1, column=5, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=2, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.ENDMARKER,
                        string="",
                        start_pos=(2, 0),
                        end_pos=(2, 0),
                        whitespace_before=WhitespaceState(
                            line=2, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=2, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                ),
            ),
            "with_indent_py37": (
                "if foo:\n    bar\n",
                _PY37,
                (
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="if",
                        start_pos=(1, 0),
                        end_pos=(1, 2),
                        whitespace_before=WhitespaceState(
                            line=1, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=2, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="foo",
                        start_pos=(1, 3),
                        end_pos=(1, 6),
                        whitespace_before=WhitespaceState(
                            line=1, column=2, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=6, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.OP,
                        string=":",
                        start_pos=(1, 6),
                        end_pos=(1, 7),
                        whitespace_before=WhitespaceState(
                            line=1, column=6, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=7, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NEWLINE,
                        string="\n",
                        start_pos=(1, 7),
                        end_pos=(2, 0),
                        whitespace_before=WhitespaceState(
                            line=1, column=7, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.INDENT,
                        string="",
                        start_pos=(2, 4),
                        end_pos=(2, 4),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent="    ",
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="bar",
                        start_pos=(2, 4),
                        end_pos=(2, 7),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=7,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NEWLINE,
                        string="\n",
                        start_pos=(2, 7),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=7,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.DEDENT,
                        string="",
                        start_pos=(3, 0),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.ENDMARKER,
                        string="",
                        start_pos=(3, 0),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                ),
            ),
            "async_py37": (
                "async def foo():\n    return await bar\n",
                _PY37,
                (
                    Token(
                        type=PythonTokenTypes.ASYNC,
                        string="async",
                        start_pos=(1, 0),
                        end_pos=(1, 5),
                        whitespace_before=WhitespaceState(
                            line=1, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=5, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="def",
                        start_pos=(1, 6),
                        end_pos=(1, 9),
                        whitespace_before=WhitespaceState(
                            line=1, column=5, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=9, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="foo",
                        start_pos=(1, 10),
                        end_pos=(1, 13),
                        whitespace_before=WhitespaceState(
                            line=1, column=9, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1,
                            column=13,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.OP,
                        string="(",
                        start_pos=(1, 13),
                        end_pos=(1, 14),
                        whitespace_before=WhitespaceState(
                            line=1,
                            column=13,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=14, absolute_indent="", is_parenthesized=True
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.OP,
                        string=")",
                        start_pos=(1, 14),
                        end_pos=(1, 15),
                        whitespace_before=WhitespaceState(
                            line=1, column=14, absolute_indent="", is_parenthesized=True
                        ),
                        whitespace_after=WhitespaceState(
                            line=1,
                            column=15,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.OP,
                        string=":",
                        start_pos=(1, 15),
                        end_pos=(1, 16),
                        whitespace_before=WhitespaceState(
                            line=1,
                            column=15,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=1,
                            column=16,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NEWLINE,
                        string="\n",
                        start_pos=(1, 16),
                        end_pos=(2, 0),
                        whitespace_before=WhitespaceState(
                            line=1,
                            column=16,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.INDENT,
                        string="",
                        start_pos=(2, 4),
                        end_pos=(2, 4),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent="    ",
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="return",
                        start_pos=(2, 4),
                        end_pos=(2, 10),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=10,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.AWAIT,
                        string="await",
                        start_pos=(2, 11),
                        end_pos=(2, 16),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=10,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=16,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="bar",
                        start_pos=(2, 17),
                        end_pos=(2, 20),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=16,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=20,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NEWLINE,
                        string="\n",
                        start_pos=(2, 20),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=20,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.DEDENT,
                        string="",
                        start_pos=(3, 0),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.ENDMARKER,
                        string="",
                        start_pos=(3, 0),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                ),
            ),
            "simple_py38": (
                "pass;\n",
                _PY38,
                (
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="pass",
                        start_pos=(1, 0),
                        end_pos=(1, 4),
                        whitespace_before=WhitespaceState(
                            line=1, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=4, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.OP,
                        string=";",
                        start_pos=(1, 4),
                        end_pos=(1, 5),
                        whitespace_before=WhitespaceState(
                            line=1, column=4, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=5, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NEWLINE,
                        string="\n",
                        start_pos=(1, 5),
                        end_pos=(2, 0),
                        whitespace_before=WhitespaceState(
                            line=1, column=5, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=2, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.ENDMARKER,
                        string="",
                        start_pos=(2, 0),
                        end_pos=(2, 0),
                        whitespace_before=WhitespaceState(
                            line=2, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=2, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                ),
            ),
            "with_indent_py38": (
                "if foo:\n    bar\n",
                _PY38,
                (
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="if",
                        start_pos=(1, 0),
                        end_pos=(1, 2),
                        whitespace_before=WhitespaceState(
                            line=1, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=2, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="foo",
                        start_pos=(1, 3),
                        end_pos=(1, 6),
                        whitespace_before=WhitespaceState(
                            line=1, column=2, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=6, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.OP,
                        string=":",
                        start_pos=(1, 6),
                        end_pos=(1, 7),
                        whitespace_before=WhitespaceState(
                            line=1, column=6, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=7, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NEWLINE,
                        string="\n",
                        start_pos=(1, 7),
                        end_pos=(2, 0),
                        whitespace_before=WhitespaceState(
                            line=1, column=7, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.INDENT,
                        string="",
                        start_pos=(2, 4),
                        end_pos=(2, 4),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent="    ",
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="bar",
                        start_pos=(2, 4),
                        end_pos=(2, 7),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=7,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NEWLINE,
                        string="\n",
                        start_pos=(2, 7),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=7,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.DEDENT,
                        string="",
                        start_pos=(3, 0),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.ENDMARKER,
                        string="",
                        start_pos=(3, 0),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                ),
            ),
            "async_py38": (
                "async def foo():\n    return await bar\n",
                _PY38,
                (
                    Token(
                        type=PythonTokenTypes.ASYNC,
                        string="async",
                        start_pos=(1, 0),
                        end_pos=(1, 5),
                        whitespace_before=WhitespaceState(
                            line=1, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=5, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="def",
                        start_pos=(1, 6),
                        end_pos=(1, 9),
                        whitespace_before=WhitespaceState(
                            line=1, column=5, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=9, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="foo",
                        start_pos=(1, 10),
                        end_pos=(1, 13),
                        whitespace_before=WhitespaceState(
                            line=1, column=9, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=1,
                            column=13,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.OP,
                        string="(",
                        start_pos=(1, 13),
                        end_pos=(1, 14),
                        whitespace_before=WhitespaceState(
                            line=1,
                            column=13,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=1, column=14, absolute_indent="", is_parenthesized=True
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.OP,
                        string=")",
                        start_pos=(1, 14),
                        end_pos=(1, 15),
                        whitespace_before=WhitespaceState(
                            line=1, column=14, absolute_indent="", is_parenthesized=True
                        ),
                        whitespace_after=WhitespaceState(
                            line=1,
                            column=15,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.OP,
                        string=":",
                        start_pos=(1, 15),
                        end_pos=(1, 16),
                        whitespace_before=WhitespaceState(
                            line=1,
                            column=15,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=1,
                            column=16,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NEWLINE,
                        string="\n",
                        start_pos=(1, 16),
                        end_pos=(2, 0),
                        whitespace_before=WhitespaceState(
                            line=1,
                            column=16,
                            absolute_indent="",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.INDENT,
                        string="",
                        start_pos=(2, 4),
                        end_pos=(2, 4),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent="    ",
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="return",
                        start_pos=(2, 4),
                        end_pos=(2, 10),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=0,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=10,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.AWAIT,
                        string="await",
                        start_pos=(2, 11),
                        end_pos=(2, 16),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=10,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=16,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NAME,
                        string="bar",
                        start_pos=(2, 17),
                        end_pos=(2, 20),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=16,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=2,
                            column=20,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.NEWLINE,
                        string="\n",
                        start_pos=(2, 20),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=2,
                            column=20,
                            absolute_indent="    ",
                            is_parenthesized=False,
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.DEDENT,
                        string="",
                        start_pos=(3, 0),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                    Token(
                        type=PythonTokenTypes.ENDMARKER,
                        string="",
                        start_pos=(3, 0),
                        end_pos=(3, 0),
                        whitespace_before=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        whitespace_after=WhitespaceState(
                            line=3, column=0, absolute_indent="", is_parenthesized=False
                        ),
                        relative_indent=None,
                    ),
                ),
            ),
        }
    )
    def test_tokenize(
        self, code: str, ver: PythonVersionInfo, expected: Sequence[Token]
    ) -> None:
        tokens = tuple(tokenize(code, ver))
        self.assertSequenceEqual(tokens, expected)
        for a, b in zip(tokens, tokens[1:]):
            # These must be the same object, so if whitespace gets consumed (mutated) at
            # the end of token a, it shows up at the beginning of token b.
            self.assertIs(a.whitespace_after, b.whitespace_before)

    def test_errortoken(self) -> None:
        for version in [_PY36, _PY37, _PY38]:
            with self.assertRaisesRegex(ParserSyntaxError, "not a valid token"):
                # use tuple() to read everything
                # The copyright symbol isn't a valid token
                tuple(tokenize("\u00a9", version))

    def test_error_dedent(self) -> None:
        for version in [_PY36, _PY37, _PY38]:
            with self.assertRaisesRegex(ParserSyntaxError, "Inconsistent indentation"):
                # create some inconsistent indents to generate an ERROR_DEDENT token
                tuple(tokenize("    a\n  b", version))
