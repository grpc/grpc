# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


import re
from contextlib import contextmanager
from dataclasses import dataclass, field
from typing import Iterator, List, Optional, Pattern

from libcst._add_slots import add_slots
from libcst._nodes.base import CSTNode
from libcst._nodes.internal import CodegenState
from libcst._nodes.module import Module
from libcst._position import CodePosition, CodeRange
from libcst.metadata.base_provider import BaseMetadataProvider

NEWLINE_RE: Pattern[str] = re.compile(r"\r\n?|\n")


@add_slots
@dataclass(frozen=False)
class WhitespaceInclusivePositionProvidingCodegenState(CodegenState):
    # These are derived from a Module
    default_indent: str
    default_newline: str
    provider: BaseMetadataProvider[CodeRange]

    indent_tokens: List[str] = field(default_factory=list)
    tokens: List[str] = field(default_factory=list)

    line: int = 1  # one-indexed
    column: int = 0  # zero-indexed
    _stack: List[CodePosition] = field(init=False, default_factory=list)

    def add_indent_tokens(self) -> None:
        self.tokens.extend(self.indent_tokens)
        for token in self.indent_tokens:
            self._update_position(token)

    def add_token(self, value: str) -> None:
        self.tokens.append(value)
        self._update_position(value)

    def _update_position(self, value: str) -> None:
        """
        Computes new line and column numbers from adding the token [value].
        """
        segments = NEWLINE_RE.split(value)
        if len(segments) == 1:  # contains no newlines
            # no change to self.lines
            self.column += len(value)
        else:
            self.line += len(segments) - 1
            # newline resets column back to 0, but a trailing token may shift column
            self.column = len(segments[-1])

    def before_codegen(self, node: "CSTNode") -> None:
        self._stack.append(CodePosition(self.line, self.column))

    def after_codegen(self, node: "CSTNode") -> None:
        # we must unconditionally pop the stack, else we could end up in a broken state
        start_pos = self._stack.pop()

        # Don't overwrite existing position information
        # (i.e. semantic position has already been recorded)
        if node not in self.provider._computed:
            end_pos = CodePosition(self.line, self.column)
            node_range = CodeRange(start_pos, end_pos)
            self.provider._computed[node] = node_range


class WhitespaceInclusivePositionProvider(BaseMetadataProvider[CodeRange]):
    """
    Generates line and column metadata.

    The start and ending bounds of the positions produced by this provider include all
    whitespace owned by the node.
    """

    def _gen_impl(self, module: Module) -> None:
        state = WhitespaceInclusivePositionProvidingCodegenState(
            default_indent=module.default_indent,
            default_newline=module.default_newline,
            provider=self,
        )
        module._codegen(state)


@add_slots
@dataclass(frozen=False)
class PositionProvidingCodegenState(WhitespaceInclusivePositionProvidingCodegenState):
    @contextmanager
    def record_syntactic_position(
        self,
        node: CSTNode,
        *,
        start_node: Optional[CSTNode] = None,
        end_node: Optional[CSTNode] = None,
    ) -> Iterator[None]:
        start = CodePosition(self.line, self.column)
        try:
            yield
        finally:
            end = CodePosition(self.line, self.column)

            # Override with positions hoisted from child nodes if provided
            start = (
                self.provider._computed[start_node].start
                if start_node is not None
                else start
            )
            end = self.provider._computed[end_node].end if end_node is not None else end

            self.provider._computed[node] = CodeRange(start, end)


class PositionProvider(BaseMetadataProvider[CodeRange]):
    """
    Generates line and column metadata.

    These positions are defined by the start and ending bounds of a node ignoring most
    instances of leading and trailing whitespace when it is not syntactically
    significant.

    The positions provided by this provider should eventually match the positions used
    by `Pyre <https://github.com/facebook/pyre-check>`__ for equivalent nodes.
    """

    def _gen_impl(self, module: Module) -> None:
        state = PositionProvidingCodegenState(
            default_indent=module.default_indent,
            default_newline=module.default_newline,
            provider=self,
        )
        module._codegen(state)
