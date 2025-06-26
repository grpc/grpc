# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


"""
Parso's tokenize doesn't give us tokens in the format that we'd ideally like, so this
performs a small number of transformations to the token stream:

- `end_pos` is precomputed as a property, instead of lazily as a method, for more
  efficient access.
- `whitespace_before` and `whitespace_after` have been added. These include the correct
  indentation information.
- `prefix` is removed, since we don't use it anywhere.
- `ERRORTOKEN` and `ERROR_DEDENT` have been removed, because we don't intend to support
  error recovery. If we encounter token errors, we'll raise a ParserSyntaxError instead.

If performance becomes a concern, we can rewrite this later as a fork of the original
tokenize module, instead of as a wrapper.
"""

from dataclasses import dataclass, field
from enum import Enum
from typing import Generator, Iterator, List, Optional, Sequence

from libcst._add_slots import add_slots
from libcst._exceptions import ParserSyntaxError
from libcst._parser.parso.python.token import PythonTokenTypes, TokenType
from libcst._parser.parso.python.tokenize import (
    Token as OrigToken,
    tokenize_lines as orig_tokenize_lines,
)
from libcst._parser.parso.utils import PythonVersionInfo, split_lines
from libcst._parser.types.token import Token
from libcst._parser.types.whitespace_state import WhitespaceState

_ERRORTOKEN: TokenType = PythonTokenTypes.ERRORTOKEN
_ERROR_DEDENT: TokenType = PythonTokenTypes.ERROR_DEDENT

_INDENT: TokenType = PythonTokenTypes.INDENT
_DEDENT: TokenType = PythonTokenTypes.DEDENT
_ENDMARKER: TokenType = PythonTokenTypes.ENDMARKER

_FSTRING_START: TokenType = PythonTokenTypes.FSTRING_START
_FSTRING_END: TokenType = PythonTokenTypes.FSTRING_END

_OP: TokenType = PythonTokenTypes.OP


class _ParenthesisOrFStringStackEntry(Enum):
    PARENTHESIS = 0
    FSTRING = 0


_PARENTHESIS_STACK_ENTRY: _ParenthesisOrFStringStackEntry = (
    _ParenthesisOrFStringStackEntry.PARENTHESIS
)
_FSTRING_STACK_ENTRY: _ParenthesisOrFStringStackEntry = (
    _ParenthesisOrFStringStackEntry.FSTRING
)


@add_slots
@dataclass(frozen=False)
class _TokenizeState:
    lines: Sequence[str]
    previous_whitespace_state: WhitespaceState = field(
        default_factory=lambda: WhitespaceState(
            line=1, column=0, absolute_indent="", is_parenthesized=False
        )
    )
    indents: List[str] = field(default_factory=lambda: [""])
    parenthesis_or_fstring_stack: List[_ParenthesisOrFStringStackEntry] = field(
        default_factory=list
    )


def tokenize(code: str, version_info: PythonVersionInfo) -> Iterator[Token]:
    try:
        from libcst_native import tokenize as native_tokenize

        return native_tokenize.tokenize(code)
    except ImportError:
        lines = split_lines(code, keepends=True)
        return tokenize_lines(code, lines, version_info)


def tokenize_lines(
    code: str, lines: Sequence[str], version_info: PythonVersionInfo
) -> Iterator[Token]:
    try:
        from libcst_native import tokenize as native_tokenize

        # TODO: pass through version_info
        return native_tokenize.tokenize(code)
    except ImportError:
        return tokenize_lines_py(code, lines, version_info)


def tokenize_lines_py(
    code: str, lines: Sequence[str], version_info: PythonVersionInfo
) -> Generator[Token, None, None]:
    state = _TokenizeState(lines)
    orig_tokens_iter = iter(orig_tokenize_lines(lines, version_info))

    # Iterate over the tokens and pass them to _convert_token, providing a one-token
    # lookahead, to enable proper indent handling.
    try:
        curr_token = next(orig_tokens_iter)
    except StopIteration:
        pass  # empty file
    else:
        for next_token in orig_tokens_iter:
            yield _convert_token(state, curr_token, next_token)
            curr_token = next_token
        yield _convert_token(state, curr_token, None)


def _convert_token(  # noqa: C901: too complex
    state: _TokenizeState, curr_token: OrigToken, next_token: Optional[OrigToken]
) -> Token:
    ct_type = curr_token.type
    ct_string = curr_token.string
    ct_start_pos = curr_token.start_pos
    if ct_type is _ERRORTOKEN:
        raise ParserSyntaxError(
            f"{ct_string!r} is not a valid token.",
            lines=state.lines,
            raw_line=ct_start_pos[0],
            raw_column=ct_start_pos[1],
        )
    if ct_type is _ERROR_DEDENT:
        raise ParserSyntaxError(
            "Inconsistent indentation. Expected a dedent.",
            lines=state.lines,
            raw_line=ct_start_pos[0],
            raw_column=ct_start_pos[1],
        )

    # Compute relative indent changes for indent/dedent nodes
    relative_indent: Optional[str] = None
    if ct_type is _INDENT:
        old_indent = "" if len(state.indents) < 2 else state.indents[-2]
        new_indent = state.indents[-1]
        relative_indent = new_indent[len(old_indent) :]

    if next_token is not None:
        nt_type = next_token.type
        if nt_type is _INDENT:
            nt_line, nt_column = next_token.start_pos
            state.indents.append(state.lines[nt_line - 1][:nt_column])
        elif nt_type is _DEDENT:
            state.indents.pop()

    whitespace_before = state.previous_whitespace_state

    if ct_type is _INDENT or ct_type is _DEDENT or ct_type is _ENDMARKER:
        # Don't update whitespace state for these dummy tokens. This makes it possible
        # to partially parse whitespace for IndentedBlock footers, and then parse the
        # rest of the whitespace in the following statement's leading_lines.
        # Unfortunately, that means that the indentation is either wrong for the footer
        # comments, or for the next line. We've chosen to allow it to be wrong for the
        # IndentedBlock footer and manually override the state when parsing whitespace
        # in that particular node.
        whitespace_after = whitespace_before
        ct_end_pos = ct_start_pos
    else:
        # Not a dummy token, so update the whitespace state.

        # Compute our own end_pos, since parso's end_pos is wrong for triple-strings.
        lines = split_lines(ct_string)
        if len(lines) > 1:
            ct_end_pos = ct_start_pos[0] + len(lines) - 1, len(lines[-1])
        else:
            ct_end_pos = (ct_start_pos[0], ct_start_pos[1] + len(ct_string))

        # Figure out what mode the whitespace parser should use. If we're inside
        # parentheses, certain whitespace (e.g. newlines) are allowed where they would
        # otherwise not be. f-strings override and disable this behavior, however.
        #
        # Parso's tokenizer tracks this internally, but doesn't expose it, so we have to
        # duplicate that logic here.

        pof_stack = state.parenthesis_or_fstring_stack
        try:
            if ct_type is _FSTRING_START:
                pof_stack.append(_FSTRING_STACK_ENTRY)
            elif ct_type is _FSTRING_END:
                pof_stack.pop()
            elif ct_type is _OP:
                if ct_string in "([{":
                    pof_stack.append(_PARENTHESIS_STACK_ENTRY)
                elif ct_string in ")]}":
                    pof_stack.pop()
        except IndexError:
            # pof_stack may be empty by the time we need to read from it due to
            # mismatched braces.
            raise ParserSyntaxError(
                "Encountered a closing brace without a matching opening brace.",
                lines=state.lines,
                raw_line=ct_start_pos[0],
                raw_column=ct_start_pos[1],
            )
        is_parenthesized = (
            len(pof_stack) > 0 and pof_stack[-1] == _PARENTHESIS_STACK_ENTRY
        )

        whitespace_after = WhitespaceState(
            ct_end_pos[0], ct_end_pos[1], state.indents[-1], is_parenthesized
        )

    # Hold onto whitespace_after, so we can use it as whitespace_before in the next
    # node.
    state.previous_whitespace_state = whitespace_after

    return Token(
        ct_type,
        ct_string,
        ct_start_pos,
        ct_end_pos,
        whitespace_before,
        whitespace_after,
        relative_indent,
    )
