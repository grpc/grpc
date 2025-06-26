# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from dataclasses import dataclass

from libcst._add_slots import add_slots


@add_slots
@dataclass(frozen=False)
class WhitespaceState:
    """
    A frequently mutated store of the whitespace parser's current state. This object
    must be cloned prior to speculative parsing.

    This is in contrast to the `config` object each whitespace parser function takes,
    which is frozen and never mutated.

    Whitespace parsing works by mutating this state object. By encapsulating saving, and
    re-using state objects inside the top-level python parser, the whitespace parser is
    able to be reentrant. One 'convert' function can consume part of the whitespace, and
    another 'convert' function can consume the rest, depending on who owns what
    whitespace.

    This is similar to the approach you might take to parse nested languages (e.g.
    JavaScript inside of HTML). We're treating whitespace as a separate language and
    grammar from the rest of Python's grammar.
    """

    line: int  # one-indexed (to match parso's behavior)
    column: int  # zero-indexed (to match parso's behavior)
    # What to look for when executing `_parse_indent`.
    absolute_indent: str
    is_parenthesized: bool
