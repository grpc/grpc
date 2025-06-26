# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import List, Optional, Sequence, Tuple, Union

from libcst import CSTLogicError, ParserSyntaxError
from libcst._nodes.whitespace import (
    Comment,
    COMMENT_RE,
    EmptyLine,
    Newline,
    NEWLINE_RE,
    ParenthesizedWhitespace,
    SIMPLE_WHITESPACE_RE,
    SimpleWhitespace,
    TrailingWhitespace,
)
from libcst._parser.types.config import BaseWhitespaceParserConfig
from libcst._parser.types.whitespace_state import WhitespaceState as State

# BEGIN PARSER ENTRYPOINTS


def parse_simple_whitespace(
    config: BaseWhitespaceParserConfig, state: State
) -> SimpleWhitespace:
    # The match never fails because the pattern can match an empty string
    lines = config.lines
    # pyre-fixme[16]: Optional type has no attribute `group`.
    ws_line = SIMPLE_WHITESPACE_RE.match(lines[state.line - 1], state.column).group(0)
    ws_line_list = [ws_line]
    while "\\" in ws_line:
        # continuation character
        state.line += 1
        state.column = 0
        ws_line = SIMPLE_WHITESPACE_RE.match(lines[state.line - 1], state.column).group(
            0
        )
        ws_line_list.append(ws_line)

    # TODO: we could special-case the common case where there's no continuation
    # character to avoid list construction and joining.

    # once we've finished collecting continuation characters
    state.column += len(ws_line)
    return SimpleWhitespace("".join(ws_line_list))


def parse_empty_lines(
    config: BaseWhitespaceParserConfig,
    state: State,
    *,
    override_absolute_indent: Optional[str] = None,
) -> Sequence[EmptyLine]:
    # If override_absolute_indent is true, then we need to parse all lines up
    # to and including the last line that is indented at our level. These all
    # belong to the footer and not to the next line's leading_lines. All lines
    # that have indent=False and come after the last line where indent=True
    # do not belong to this node.
    state_for_line = State(
        state.line, state.column, state.absolute_indent, state.is_parenthesized
    )
    lines: List[Tuple[State, EmptyLine]] = []
    while True:
        el = _parse_empty_line(
            config, state_for_line, override_absolute_indent=override_absolute_indent
        )
        if el is None:
            break

        # Store the updated state with the element we parsed. Then make a new state
        # clone for the next element.
        lines.append((state_for_line, el))
        state_for_line = State(
            state_for_line.line,
            state_for_line.column,
            state.absolute_indent,
            state.is_parenthesized,
        )

    if override_absolute_indent is not None:
        # We need to find the last element that is indented, and then split the list
        # at that point.
        for i in range(len(lines) - 1, -1, -1):
            if lines[i][1].indent:
                lines = lines[: (i + 1)]
                break
        else:
            # We didn't find any lines, throw them all away
            lines = []

    if lines:
        # Update the state line and column to match the last line actually parsed.
        final_state: State = lines[-1][0]
        state.line = final_state.line
        state.column = final_state.column
    return [r[1] for r in lines]


def parse_trailing_whitespace(
    config: BaseWhitespaceParserConfig, state: State
) -> TrailingWhitespace:
    trailing_whitespace = _parse_trailing_whitespace(config, state)
    if trailing_whitespace is None:
        raise ParserSyntaxError(
            "Internal Error: Failed to parse TrailingWhitespace. This should never "
            + "happen because a TrailingWhitespace is never optional in the grammar, "
            + "so this error should've been caught by parso first.",
            lines=config.lines,
            raw_line=state.line,
            raw_column=state.column,
        )
    return trailing_whitespace


def parse_parenthesizable_whitespace(
    config: BaseWhitespaceParserConfig, state: State
) -> Union[SimpleWhitespace, ParenthesizedWhitespace]:
    if state.is_parenthesized:
        # First, try parenthesized (don't need speculation because it either
        # parses or doesn't modify state).
        parenthesized_whitespace = _parse_parenthesized_whitespace(config, state)
        if parenthesized_whitespace is not None:
            return parenthesized_whitespace
    # Now, just parse and return a simple whitespace
    return parse_simple_whitespace(config, state)


# END PARSER ENTRYPOINTS
# BEGIN PARSER INTERNAL PRODUCTIONS


def _parse_empty_line(
    config: BaseWhitespaceParserConfig,
    state: State,
    *,
    override_absolute_indent: Optional[str] = None,
) -> Optional[EmptyLine]:
    # begin speculative parsing
    speculative_state = State(
        state.line, state.column, state.absolute_indent, state.is_parenthesized
    )
    try:
        indent = _parse_indent(
            config, speculative_state, override_absolute_indent=override_absolute_indent
        )
    except Exception:
        # We aren't on a new line, speculative parsing failed
        return None
    whitespace = parse_simple_whitespace(config, speculative_state)
    comment = _parse_comment(config, speculative_state)
    newline = _parse_newline(config, speculative_state)
    if newline is None:
        # speculative parsing failed
        return None
    # speculative parsing succeeded
    state.line = speculative_state.line
    state.column = speculative_state.column
    # don't need to copy absolute_indent/is_parenthesized because they don't change.
    return EmptyLine(indent, whitespace, comment, newline)


def _parse_indent(
    config: BaseWhitespaceParserConfig,
    state: State,
    *,
    override_absolute_indent: Optional[str] = None,
) -> bool:
    """
    Returns True if indentation was found, otherwise False.
    """
    absolute_indent = (
        override_absolute_indent
        if override_absolute_indent is not None
        else state.absolute_indent
    )
    line_str = config.lines[state.line - 1]
    if state.column != 0:
        if state.column == len(line_str) and state.line == len(config.lines):
            # We're at EOF, treat this as a failed speculative parse
            return False
        raise CSTLogicError(
            "Internal Error: Column should be 0 when parsing an indent."
        )
    if line_str.startswith(absolute_indent, state.column):
        state.column += len(absolute_indent)
        return True
    return False


def _parse_comment(
    config: BaseWhitespaceParserConfig, state: State
) -> Optional[Comment]:
    comment_match = COMMENT_RE.match(config.lines[state.line - 1], state.column)
    if comment_match is None:
        return None
    comment = comment_match.group(0)
    state.column += len(comment)
    return Comment(comment)


def _parse_newline(
    config: BaseWhitespaceParserConfig, state: State
) -> Optional[Newline]:
    # begin speculative parsing
    line_str = config.lines[state.line - 1]
    newline_match = NEWLINE_RE.match(line_str, state.column)
    if newline_match is not None:
        # speculative parsing succeeded
        newline_str = newline_match.group(0)
        state.column += len(newline_str)
        if state.column != len(line_str):
            raise ParserSyntaxError(
                "Internal Error: Found a newline, but it wasn't the EOL.",
                lines=config.lines,
                raw_line=state.line,
                raw_column=state.column,
            )
        if state.line < len(config.lines):
            # this newline was the end of a line, and there's another line,
            # therefore we should move to the next line
            state.line += 1
            state.column = 0
        if newline_str == config.default_newline:
            # Just inherit it from the Module instead of explicitly setting it.
            return Newline()
        else:
            return Newline(newline_str)
    else:  # no newline was found, speculative parsing failed
        return None


def _parse_trailing_whitespace(
    config: BaseWhitespaceParserConfig, state: State
) -> Optional[TrailingWhitespace]:
    # Begin speculative parsing
    speculative_state = State(
        state.line, state.column, state.absolute_indent, state.is_parenthesized
    )
    whitespace = parse_simple_whitespace(config, speculative_state)
    comment = _parse_comment(config, speculative_state)
    newline = _parse_newline(config, speculative_state)
    if newline is None:
        # Speculative parsing failed
        return None
    # Speculative parsing succeeded
    state.line = speculative_state.line
    state.column = speculative_state.column
    # don't need to copy absolute_indent/is_parenthesized because they don't change.
    return TrailingWhitespace(whitespace, comment, newline)


def _parse_parenthesized_whitespace(
    config: BaseWhitespaceParserConfig, state: State
) -> Optional[ParenthesizedWhitespace]:
    first_line = _parse_trailing_whitespace(config, state)
    if first_line is None:
        # Speculative parsing failed
        return None
    empty_lines = ()
    while True:
        empty_line = _parse_empty_line(config, state)
        if empty_line is None:
            # This isn't an empty line, so parse it below
            break
        empty_lines = empty_lines + (empty_line,)
    indent = _parse_indent(config, state)
    last_line = parse_simple_whitespace(config, state)
    return ParenthesizedWhitespace(first_line, empty_lines, indent, last_line)
