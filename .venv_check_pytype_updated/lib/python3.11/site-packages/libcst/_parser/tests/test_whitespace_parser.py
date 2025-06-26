# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Callable, TypeVar

import libcst as cst
from libcst._nodes.deep_equals import deep_equals
from libcst._parser.types.config import MockWhitespaceParserConfig as Config
from libcst._parser.types.whitespace_state import WhitespaceState as State
from libcst._parser.whitespace_parser import (
    parse_empty_lines,
    parse_simple_whitespace,
    parse_trailing_whitespace,
)
from libcst.testing.utils import data_provider, UnitTest

_T = TypeVar("_T")


class WhitespaceParserTest(UnitTest):
    @data_provider(
        {
            "simple_whitespace_empty": {
                "parser": parse_simple_whitespace,
                "config": Config(
                    lines=["not whitespace\n", " another line\n"], default_newline="\n"
                ),
                "start_state": State(
                    line=1, column=0, absolute_indent="", is_parenthesized=False
                ),
                "end_state": State(
                    line=1, column=0, absolute_indent="", is_parenthesized=False
                ),
                "expected_node": cst.SimpleWhitespace(""),
            },
            "simple_whitespace_start_of_line": {
                "parser": parse_simple_whitespace,
                "config": Config(
                    lines=["\t  <-- There's some whitespace there\n"],
                    default_newline="\n",
                ),
                "start_state": State(
                    line=1, column=0, absolute_indent="", is_parenthesized=False
                ),
                "end_state": State(
                    line=1, column=3, absolute_indent="", is_parenthesized=False
                ),
                "expected_node": cst.SimpleWhitespace("\t  "),
            },
            "simple_whitespace_end_of_line": {
                "parser": parse_simple_whitespace,
                "config": Config(lines=["prefix   "], default_newline="\n"),
                "start_state": State(
                    line=1, column=6, absolute_indent="", is_parenthesized=False
                ),
                "end_state": State(
                    line=1, column=9, absolute_indent="", is_parenthesized=False
                ),
                "expected_node": cst.SimpleWhitespace("   "),
            },
            "simple_whitespace_line_continuation": {
                "parser": parse_simple_whitespace,
                "config": Config(
                    lines=["prefix \\\n", "    \\\n", "    # suffix\n"],
                    default_newline="\n",
                ),
                "start_state": State(
                    line=1, column=6, absolute_indent="", is_parenthesized=False
                ),
                "end_state": State(
                    line=3, column=4, absolute_indent="", is_parenthesized=False
                ),
                "expected_node": cst.SimpleWhitespace(" \\\n    \\\n    "),
            },
            "empty_lines_empty_list": {
                "parser": parse_empty_lines,
                "config": Config(
                    lines=["this is not an empty line"], default_newline="\n"
                ),
                "start_state": State(
                    line=1, column=0, absolute_indent="", is_parenthesized=False
                ),
                "end_state": State(
                    line=1, column=0, absolute_indent="", is_parenthesized=False
                ),
                "expected_node": [],
            },
            "empty_lines_single_line": {
                "parser": parse_empty_lines,
                "config": Config(
                    lines=["    # comment\n", "this is not an empty line\n"],
                    default_newline="\n",
                ),
                "start_state": State(
                    line=1, column=0, absolute_indent="    ", is_parenthesized=False
                ),
                "end_state": State(
                    line=2, column=0, absolute_indent="    ", is_parenthesized=False
                ),
                "expected_node": [
                    cst.EmptyLine(
                        indent=True,
                        whitespace=cst.SimpleWhitespace(""),
                        comment=cst.Comment("# comment"),
                        newline=cst.Newline(),
                    )
                ],
            },
            "empty_lines_multiple": {
                "parser": parse_empty_lines,
                "config": Config(
                    lines=[
                        "\n",
                        "    \n",
                        "     # comment with indent and whitespace\n",
                        "# comment without indent\n",
                        "  # comment with no indent but some whitespace\n",
                    ],
                    default_newline="\n",
                ),
                "start_state": State(
                    line=1, column=0, absolute_indent="    ", is_parenthesized=False
                ),
                "end_state": State(
                    line=5, column=47, absolute_indent="    ", is_parenthesized=False
                ),
                "expected_node": [
                    cst.EmptyLine(
                        indent=False,
                        whitespace=cst.SimpleWhitespace(""),
                        comment=None,
                        newline=cst.Newline(),
                    ),
                    cst.EmptyLine(
                        indent=True,
                        whitespace=cst.SimpleWhitespace(""),
                        comment=None,
                        newline=cst.Newline(),
                    ),
                    cst.EmptyLine(
                        indent=True,
                        whitespace=cst.SimpleWhitespace(" "),
                        comment=cst.Comment("# comment with indent and whitespace"),
                        newline=cst.Newline(),
                    ),
                    cst.EmptyLine(
                        indent=False,
                        whitespace=cst.SimpleWhitespace(""),
                        comment=cst.Comment("# comment without indent"),
                        newline=cst.Newline(),
                    ),
                    cst.EmptyLine(
                        indent=False,
                        whitespace=cst.SimpleWhitespace("  "),
                        comment=cst.Comment(
                            "# comment with no indent but some whitespace"
                        ),
                        newline=cst.Newline(),
                    ),
                ],
            },
            "empty_lines_non_default_newline": {
                "parser": parse_empty_lines,
                "config": Config(lines=["\n", "\r\n", "\r"], default_newline="\n"),
                "start_state": State(
                    line=1, column=0, absolute_indent="", is_parenthesized=False
                ),
                "end_state": State(
                    line=3, column=1, absolute_indent="", is_parenthesized=False
                ),
                "expected_node": [
                    cst.EmptyLine(
                        indent=True,
                        whitespace=cst.SimpleWhitespace(""),
                        comment=None,
                        newline=cst.Newline(None),  # default newline
                    ),
                    cst.EmptyLine(
                        indent=True,
                        whitespace=cst.SimpleWhitespace(""),
                        comment=None,
                        newline=cst.Newline("\r\n"),  # non-default
                    ),
                    cst.EmptyLine(
                        indent=True,
                        whitespace=cst.SimpleWhitespace(""),
                        comment=None,
                        newline=cst.Newline("\r"),  # non-default
                    ),
                ],
            },
            "trailing_whitespace": {
                "parser": parse_trailing_whitespace,
                "config": Config(
                    lines=["some code  # comment\n"], default_newline="\n"
                ),
                "start_state": State(
                    line=1, column=9, absolute_indent="", is_parenthesized=False
                ),
                "end_state": State(
                    line=1, column=21, absolute_indent="", is_parenthesized=False
                ),
                "expected_node": cst.TrailingWhitespace(
                    whitespace=cst.SimpleWhitespace("  "),
                    comment=cst.Comment("# comment"),
                    newline=cst.Newline(),
                ),
            },
        }
    )
    def test_parsers(
        self,
        parser: Callable[[Config, State], _T],
        config: Config,
        start_state: State,
        end_state: State,
        expected_node: _T,
    ) -> None:
        # Uses internal `deep_equals` function instead of `CSTNode.deep_equals`, because
        # we need to compare sequences of nodes, and this is the easiest way. :/
        parsed_node = parser(config, start_state)
        self.assertTrue(
            deep_equals(parsed_node, expected_node),
            msg=f"\n{parsed_node!r}\nis not deeply equal to \n{expected_node!r}",
        )
        self.assertEqual(start_state, end_state)
