# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


"""
Data structures used for storing position information.

These are publicly exported by metadata, but their implementation lives outside of
metadata, because they're used internally by the codegen logic, which computes position
locations.
"""

from dataclasses import dataclass
from typing import cast, overload, Tuple, Union

from libcst._add_slots import add_slots

_CodePositionT = Union[Tuple[int, int], "CodePosition"]


@add_slots
@dataclass(frozen=True)
class CodePosition:
    #: Line numbers are 1-indexed.
    line: int
    #: Column numbers are 0-indexed.
    column: int


@add_slots
@dataclass(frozen=True)
# pyre-fixme[13]: Attribute `end` is never initialized.
# pyre-fixme[13]: Attribute `start` is never initialized.
class CodeRange:
    #: Starting position of a node (inclusive).
    start: CodePosition
    #: Ending position of a node (exclusive).
    end: CodePosition

    @overload
    def __init__(self, start: CodePosition, end: CodePosition) -> None: ...

    @overload
    def __init__(self, start: Tuple[int, int], end: Tuple[int, int]) -> None: ...

    def __init__(self, start: _CodePositionT, end: _CodePositionT) -> None:
        if isinstance(start, tuple) and isinstance(end, tuple):
            object.__setattr__(self, "start", CodePosition(start[0], start[1]))
            object.__setattr__(self, "end", CodePosition(end[0], end[1]))
        else:
            start = cast(CodePosition, start)
            end = cast(CodePosition, end)
            object.__setattr__(self, "start", start)
            object.__setattr__(self, "end", end)
