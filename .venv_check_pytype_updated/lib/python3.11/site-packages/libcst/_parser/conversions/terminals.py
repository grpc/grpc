# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
# pyre-unsafe

from typing import Any

from libcst._nodes.expression import SimpleString
from libcst._parser.types.config import ParserConfig
from libcst._parser.types.partials import WithLeadingWhitespace
from libcst._parser.types.token import Token
from libcst._parser.whitespace_parser import (
    parse_empty_lines,
    parse_trailing_whitespace,
)


def convert_NAME(config: ParserConfig, token: Token) -> Any:
    return token


def convert_NUMBER(config: ParserConfig, token: Token) -> Any:
    return token


def convert_STRING(config: ParserConfig, token: Token) -> Any:
    return WithLeadingWhitespace(SimpleString(token.string), token.whitespace_before)


def convert_OP(config: ParserConfig, token: Token) -> Any:
    return token


def convert_NEWLINE(config: ParserConfig, token: Token) -> Any:
    # A NEWLINE token is only emitted for semantic newlines, which means that this
    # corresponds to a TrailingWhitespace, since that's the only semantic
    # newline-containing node.

    # N.B. Because this token is whitespace, and because the whitespace parser doesn't
    # try to prevent overflows, `token.whitespace_before` will end up overflowing into
    # the value of this newline token, so `parse_trailing_whitespace` will include
    # token.string's value. This is expected and desired behavior.
    return parse_trailing_whitespace(config, token.whitespace_before)


def convert_INDENT(config: ParserConfig, token: Token) -> Any:
    return token


def convert_DEDENT(config: ParserConfig, token: Token) -> Any:
    return token


def convert_ENDMARKER(config: ParserConfig, token: Token) -> Any:
    # Parse any and all empty lines with an indent similar to the header. That is,
    # indent of nothing and including all indents. In some cases, like when the
    # footer parser follows an indented suite, the state's indent can be wrong
    # due to the fact that it is shared with the _DEDENT node. We know that if
    # we're parsing the end of a file, we will have no indent.
    return parse_empty_lines(
        config, token.whitespace_before, override_absolute_indent=""
    )


def convert_FSTRING_START(config: ParserConfig, token: Token) -> Any:
    return token


def convert_FSTRING_END(config: ParserConfig, token: Token) -> Any:
    return token


def convert_FSTRING_STRING(config: ParserConfig, token: Token) -> Any:
    return token


def convert_ASYNC(config: ParserConfig, token: Token) -> Any:
    return token


def convert_AWAIT(config: ParserConfig, token: Token) -> Any:
    return token
