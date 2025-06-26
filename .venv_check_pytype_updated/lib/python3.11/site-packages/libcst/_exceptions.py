# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from enum import auto, Enum
from typing import Any, Callable, final, Optional, Sequence, Tuple

from libcst._tabs import expand_tabs


_NEWLINE_CHARS: str = "\r\n"


class EOFSentinel(Enum):
    EOF = auto()


class CSTLogicError(Exception):
    """General purpose internal error within LibCST itself."""

    pass


# pyre-fixme[2]: 'Any' type isn't pyre-strict.
def _parser_syntax_error_unpickle(kwargs: Any) -> "ParserSyntaxError":
    return ParserSyntaxError(**kwargs)


@final
class PartialParserSyntaxError(Exception):
    """
    An internal exception that represents a partially-constructed
    :class:`ParserSyntaxError`. It's raised by our internal parser conversion functions,
    which don't always know the current line and column information.

    This partial object only contains a message, with the expectation that the line and
    column information will be filled in by :class:`libcst._base_parser.BaseParser`.

    This should never be visible to the end-user.
    """

    message: str

    def __init__(self, message: str) -> None:
        self.message = message


@final
class ParserSyntaxError(Exception):
    """
    Contains an error encountered while trying to parse a piece of source code. This
    exception shouldn't be constructed directly by the user, but instead may be raised
    by calls to :func:`parse_module`, :func:`parse_expression`, or
    :func:`parse_statement`.

    This does not inherit from :class:`SyntaxError` because Python's may raise a
    :class:`SyntaxError` for any number of reasons, potentially leading to unintended
    behavior.
    """

    #: A human-readable explanation of the syntax error without information about where
    #: the error occurred.
    #:
    #: For a human-readable explanation of the error alongside information about where
    #: it occurred, use :meth:`__str__` (via ``str(ex)``) instead.
    message: str

    # An internal value used to compute `editor_column` and to pretty-print where the
    # syntax error occurred in the code.
    _lines: Sequence[str]

    #: The one-indexed line where the error occured.
    raw_line: int

    #: The zero-indexed column as a number of characters from the start of the line
    #: where the error occured.
    raw_column: int

    def __init__(
        self, message: str, *, lines: Sequence[str], raw_line: int, raw_column: int
    ) -> None:
        super(ParserSyntaxError, self).__init__(message)
        self.message = message
        self._lines = lines
        self.raw_line = raw_line
        self.raw_column = raw_column

    def __reduce__(
        self,
    ) -> Tuple[Callable[..., "ParserSyntaxError"], Tuple[object, ...]]:
        return (
            _parser_syntax_error_unpickle,
            (
                {
                    "message": self.message,
                    "lines": self._lines,
                    "raw_line": self.raw_line,
                    "raw_column": self.raw_column,
                },
            ),
        )

    def __str__(self) -> str:
        """
        A multi-line human-readable error message of where the syntax error is in their
        code. For example::

            Syntax Error @ 2:1.
            Incomplete input. Encountered end of file (EOF), but expected 'except', or 'finally'.

            try: pass
                     ^
        """
        context = self.context
        return (
            f"Syntax Error @ {self.editor_line}:{self.editor_column}.\n"
            + f"{self.message}"
            + (f"\n\n{context}" if context is not None else "")
        )

    def __repr__(self) -> str:
        return (
            "ParserSyntaxError("
            + f"{self.message!r}, lines=[...], raw_line={self.raw_line!r}, "
            + f"raw_column={self.raw_column!r})"
        )

    @property
    def context(self) -> Optional[str]:
        """
        A formatted string containing the line of code with the syntax error (or a
        non-empty line above it) along with a caret indicating the exact column where
        the error occurred.

        Return ``None`` if there's no relevant non-empty line to show. (e.g. the file
        consists of only blank lines)
        """
        displayed_line = self.editor_line
        displayed_column = self.editor_column
        # we want to avoid displaying a blank line for context. If we're on a blank line
        # find the nearest line above us that isn't blank.
        while displayed_line >= 1 and not len(self._lines[displayed_line - 1].strip()):
            displayed_line -= 1
            displayed_column = len(self._lines[displayed_line - 1])

        # only show context if we managed to find a non-empty line
        if len(self._lines[displayed_line - 1].strip()):
            formatted_source_line = expand_tabs(self._lines[displayed_line - 1]).rstrip(
                _NEWLINE_CHARS
            )
            # fmt: off
            return (
                f"{formatted_source_line}\n"
                + f"{' ' * (displayed_column - 1)}^"
            )
            # fmt: on
        else:
            return None

    @property
    def editor_line(self) -> int:
        """
        The expected one-indexed line in the user's editor. This is the same as
        :attr:`raw_line`.
        """
        return self.raw_line  # raw_line is already one-indexed.

    @property
    def editor_column(self) -> int:
        """
        The expected one-indexed column that's likely to match the behavior of the
        user's editor, assuming tabs expand to 1-8 spaces. This is the column number
        shown when the syntax error is printed out with `str`.

        This assumes single-width characters. However, because python doesn't ship with
        a wcwidth function, it's hard to handle this properly without a third-party
        dependency.

        For a raw zero-indexed character offset without tab expansion, see
        :attr:`raw_column`.
        """
        prefix_str = self._lines[self.raw_line - 1][: self.raw_column]
        tab_adjusted_column = len(expand_tabs(prefix_str))
        # Text editors use a one-indexed column, so we need to add one to our
        # zero-indexed column to get a human-readable result.
        return tab_adjusted_column + 1


class MetadataException(Exception):
    pass
