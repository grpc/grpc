# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
# pyre-unsafe

from typing import Any, Sequence

from libcst._nodes.module import Module
from libcst._nodes.whitespace import NEWLINE_RE
from libcst._parser.production_decorator import with_production
from libcst._parser.types.config import ParserConfig


@with_production("file_input", "(NEWLINE | stmt)* ENDMARKER")
def convert_file_input(config: ParserConfig, children: Sequence[Any]) -> Any:
    *body, footer = children
    if len(body) == 0:
        # If there's no body, the header and footer are ambiguous. The header is more
        # important, and should own the EmptyLine nodes instead of the footer.
        header = footer
        footer = ()
        if (
            len(config.lines) == 2
            and NEWLINE_RE.fullmatch(config.lines[0])
            and config.lines[1] == ""
        ):
            # This is an empty file (not even a comment), so special-case this to an
            # empty list instead of a single dummy EmptyLine (which is what we'd
            # normally parse).
            header = ()
    else:
        # Steal the leading lines from the first statement, and move them into the
        # header.
        first_stmt = body[0]
        header = first_stmt.leading_lines
        body[0] = first_stmt.with_changes(leading_lines=())
    return Module(
        header=header,
        body=body,
        footer=footer,
        encoding=config.encoding,
        default_indent=config.default_indent,
        default_newline=config.default_newline,
        has_trailing_newline=config.has_trailing_newline,
    )
