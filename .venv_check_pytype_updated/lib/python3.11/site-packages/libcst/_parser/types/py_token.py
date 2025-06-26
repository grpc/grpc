# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


from dataclasses import dataclass
from typing import Optional, Tuple

from libcst._add_slots import add_slots
from libcst._parser.parso.python.token import TokenType
from libcst._parser.types.whitespace_state import WhitespaceState


@add_slots
@dataclass(frozen=True)
class Token:
    type: TokenType
    string: str
    # The start of where `string` is in the source, not including leading whitespace.
    start_pos: Tuple[int, int]
    # The end of where `string` is in the source, not including trailing whitespace.
    end_pos: Tuple[int, int]
    whitespace_before: WhitespaceState
    whitespace_after: WhitespaceState
    # The relative indent this token adds.
    relative_indent: Optional[str]
