# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


from contextlib import contextmanager
from dataclasses import dataclass, field
from typing import Callable, Iterator, List, Optional

from libcst import CSTNode, Module
from libcst._nodes.internal import CodegenState
from libcst.metadata.base_provider import BaseMetadataProvider


@dataclass(frozen=True)
class CodeSpan:
    """
    Represents the position of a piece of code by its starting position and length.

    Note: This class does not specify the unit of distance - it can be bytes,
    Unicode characters, or something else entirely.
    """

    #: Offset of the code from the beginning of the file. Can be 0.
    start: int
    #: Length of the span
    length: int


@dataclass(frozen=False)
class SpanProvidingCodegenState(CodegenState):
    provider: BaseMetadataProvider[CodeSpan]
    get_length: Optional[Callable[[str], int]] = None
    position: int = 0
    _stack: List[int] = field(default_factory=list)

    def add_indent_tokens(self) -> None:
        super().add_indent_tokens()
        for token in self.indent_tokens:
            self._update_position(token)

    def add_token(self, value: str) -> None:
        super().add_token(value)
        self._update_position(value)

    def _update_position(self, value: str) -> None:
        get_length = self.get_length or len
        self.position += get_length(value)

    def before_codegen(self, node: CSTNode) -> None:
        self._stack.append(self.position)

    def after_codegen(self, node: CSTNode) -> None:
        start = self._stack.pop()

        if node not in self.provider._computed:
            end = self.position
            self.provider._computed[node] = CodeSpan(start, length=end - start)

    @contextmanager
    def record_syntactic_position(
        self,
        node: CSTNode,
        *,
        start_node: Optional[CSTNode] = None,
        end_node: Optional[CSTNode] = None,
    ) -> Iterator[None]:
        start = self.position
        try:
            yield
        finally:
            end = self.position
            start = (
                self.provider._computed[start_node].start
                if start_node is not None
                else start
            )
            if end_node is not None:
                end_span = self.provider._computed[end_node]
                length = (end_span.start + end_span.length) - start
            else:
                length = end - start
            self.provider._computed[node] = CodeSpan(start, length=length)


def byte_length_in_utf8(value: str) -> int:
    return len(value.encode("utf8"))


class ByteSpanPositionProvider(BaseMetadataProvider[CodeSpan]):
    """
    Generates offset and length metadata for nodes' positions.

    For each :class:`CSTNode` this provider generates a :class:`CodeSpan` that
    contains the byte-offset of the node from the start of the file, and its
    length (also in bytes). The whitespace owned by the node is not included in
    this length.

    Note: offset and length measure bytes, not characters (which is significant for
    example in the case of Unicode characters encoded in more than one byte)
    """

    def _gen_impl(self, module: Module) -> None:
        state = SpanProvidingCodegenState(
            default_indent=module.default_indent,
            default_newline=module.default_newline,
            provider=self,
            get_length=byte_length_in_utf8,
        )
        module._codegen(state)
